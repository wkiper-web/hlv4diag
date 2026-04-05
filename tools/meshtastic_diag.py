import argparse
import contextlib
import io
import json
import threading
import time
from typing import Any

import requests
from meshtastic.serial_interface import SerialInterface


def node_id_hex(node_num: int) -> str:
    return f"!{node_num:08x}"


def make_json_safe(value: Any) -> Any:
    if value is None or isinstance(value, (bool, int, float, str)):
        return value
    if isinstance(value, dict):
        return {str(key): make_json_safe(inner) for key, inner in value.items()}
    if isinstance(value, list):
        return [make_json_safe(item) for item in value]
    if isinstance(value, tuple):
        return [make_json_safe(item) for item in value]
    if isinstance(value, bytes):
        return value.hex()
    return str(value)


def summarize_neighbors(iface: SerialInterface) -> list[dict[str, Any]]:
    now = time.time()
    rows: list[dict[str, Any]] = []

    for num, node in iface.nodesByNum.items():
        if num == iface.localNode.nodeNum:
            continue

        user = node.get("user", {})
        last_heard = node.get("lastHeard")
        age_s = None if last_heard is None else round(now - last_heard, 1)

        rows.append(
            {
                "node_num": num,
                "id": user.get("id") or node_id_hex(num),
                "long_name": user.get("longName"),
                "hops": node.get("hopsAway"),
                "snr": node.get("snr"),
                "last_heard": last_heard,
                "age_s": age_s,
                "messageable": not bool(user.get("isUnmessagable")),
            }
        )

    rows.sort(
        key=lambda row: (
            row["hops"] if row["hops"] is not None else 999,
            row["age_s"] if row["age_s"] is not None else 999999,
            -(row["snr"] if row["snr"] is not None else -999),
        )
    )
    return rows


def choose_neighbor(rows: list[dict[str, Any]], explicit_id: str | None) -> dict[str, Any] | None:
    if explicit_id:
        for row in rows:
            if row["id"] == explicit_id:
                return row
        return None

    for row in rows:
        if not row["messageable"]:
            continue
        if row["hops"] is not None and row["hops"] <= 1:
            return row
    for row in rows:
        if row["messageable"]:
            return row
    return None


def run_ack_test(
    iface: SerialInterface,
    dest_id: str,
    text: str,
    timeout_s: float,
) -> dict[str, Any]:
    event = threading.Event()
    result: dict[str, Any] = {
        "dest_id": dest_id,
        "text": text,
        "timeout_s": timeout_s,
        "request_id": None,
        "response": None,
        "success": False,
    }

    def onAckNak(packet: dict[str, Any]) -> None:  # noqa: N802
        decoded = packet.get("decoded", {})
        routing = decoded.get("routing", {})
        error_reason = routing.get("errorReason", "NONE")
        result["response"] = {
            "from": packet.get("fromId") or packet.get("from"),
            "to": packet.get("toId") or packet.get("to"),
            "error_reason": error_reason,
            "raw": make_json_safe(packet),
        }
        result["success"] = error_reason == "NONE" and (
            (packet.get("fromId") or packet.get("from")) != iface.getMyNodeInfo()["user"]["id"]
        )
        event.set()

    packet = iface.sendText(
        text,
        destinationId=dest_id,
        wantAck=True,
        onResponse=onAckNak,
    )
    result["request_id"] = packet.id

    if not event.wait(timeout_s):
        result["response"] = {"error_reason": "TIMEOUT"}
        return result

    return result


def run_traceroute_test(
    iface: SerialInterface,
    dest_id: str,
    hop_limit: int,
) -> dict[str, Any]:
    capture = io.StringIO()
    success = True
    error = None
    with contextlib.redirect_stdout(capture):
        try:
            iface.sendTraceRoute(dest_id, hop_limit)
        except Exception as exc:  # pragma: no cover - diagnostic script
            success = False
            error = str(exc)
    return {
        "dest_id": dest_id,
        "hop_limit": hop_limit,
        "success": success,
        "error": error,
        "output": capture.getvalue().strip(),
    }


def poll_onemesh_for_text(root_topic: str, text: str, timeout_s: float) -> dict[str, Any]:
    started = time.time()
    attempts = 0
    last_error = None

    while time.time() - started < timeout_s:
        attempts += 1
        try:
            response = requests.get(
                "https://map.onemesh.ru/api/v1/text-messages",
                params={"root_topic": root_topic, "count": 150, "order": "desc"},
                timeout=20,
            )
            response.raise_for_status()
            messages = response.json().get("text_messages", [])
            for message in messages:
                if message.get("text") == text:
                    return {
                        "found": True,
                        "attempts": attempts,
                        "message": message,
                    }
        except Exception as exc:  # pragma: no cover - diagnostic script
            last_error = str(exc)

        time.sleep(10)

    return {
        "found": False,
        "attempts": attempts,
        "error": last_error,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Meshtastic + ONEmesh diagnostic helper")
    parser.add_argument("--port", default="COM4")
    parser.add_argument("--root-topic", default="msh/RU/YAR")
    parser.add_argument("--neighbor", default=None, help="Destination node id like !acaaa498")
    parser.add_argument("--hop-limit", type=int, default=7)
    parser.add_argument("--ack-timeout", type=float, default=25.0)
    parser.add_argument("--map-timeout", type=float, default=65.0)
    args = parser.parse_args()

    iface = SerialInterface(devPath=args.port)
    try:
        neighbors = summarize_neighbors(iface)
        chosen_neighbor = choose_neighbor(neighbors, args.neighbor)

        ack_result = None
        traceroute_result = None
        if chosen_neighbor:
            ack_text = f"diag-ack-{int(time.time())}"
            ack_result = run_ack_test(
                iface,
                chosen_neighbor["id"],
                ack_text,
                timeout_s=args.ack_timeout,
            )
            traceroute_result = run_traceroute_test(
                iface,
                chosen_neighbor["id"],
                hop_limit=args.hop_limit,
            )

        broadcast_text = f"diag-map-{int(time.time())}"
        broadcast_packet = iface.sendText(broadcast_text, wantAck=False)
        map_result = poll_onemesh_for_text(
            args.root_topic,
            broadcast_text,
            timeout_s=args.map_timeout,
        )

        output = {
            "local": {
                "node_num": iface.localNode.nodeNum,
                "id": iface.getMyNodeInfo()["user"]["id"],
                "long_name": iface.getMyNodeInfo()["user"]["longName"],
                "device_metrics": iface.nodesByNum.get(iface.localNode.nodeNum, {}).get("deviceMetrics", {}),
            },
            "neighbor_candidates": neighbors[:8],
            "chosen_neighbor": chosen_neighbor,
            "ack_test": ack_result,
            "traceroute_test": traceroute_result,
            "broadcast_test": {
                "text": broadcast_text,
                "request_id": broadcast_packet.id,
                "map_result": map_result,
            },
        }

        print(json.dumps(make_json_safe(output), ensure_ascii=False, indent=2))
        return 0
    finally:
        iface.close()


if __name__ == "__main__":
    raise SystemExit(main())
