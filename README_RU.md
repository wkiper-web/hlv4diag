# Heltec V4 Meshtastic / LoRa Diagnostics

Набор файлов для диагностики `Heltec V4`, если нода:

- слышит эфир LoRa
- видит соседей в Meshtastic
- но не получает `ACK`, не строит `traceroute` или выглядит как нода с плохим `TX`

Репозиторий подходит для двух сценариев:

1. Проверить сам радиотракт через специальный диагностический скетч.
2. Вернуть плату на Meshtastic и прогнать проверки уже в реальной сети.

## Что внутри

- `src/main.cpp`
  Диагностический скетч для прямой проверки SX1262 и RF-тракта.
- `tools/meshtastic_diag.py`
  Хостовый скрипт для проверки `ACK`, `traceroute` и контрольного broadcast через ONEmesh API.
- `meshtastic-backup-2026-04-05.yaml`
  Резервная копия конфигурации Meshtastic до изменений.
- `meshtastic-onemesh-yar-2026-04-05.yaml`
  Один из рабочих конфигов для OneMesh.
- `meshtastic-onemesh-yar-current-2026-04-05.yaml`
  Актуальный конфиг восстановленного состояния.
- `firmware_restore/firmware-heltec-v4-2.7.18.fb3bf78.factory.bin`
  Локальный `factory.bin` Meshtastic для восстановления платы.
- `docs/DIAGNOSTICS.md`
  Короткое описание, что именно проверялось и к каким выводам это привело.
- `docs/GITHUB_METADATA.md`
  Готовый текст для GitHub `About`, description и topics.

## Что нужно пользователю

Минимум:

- Windows
- Python `3.10+`
- установленный `Git`
- установленный `PlatformIO Core`
- доступ к USB-порту платы

Для хостового скрипта:

- `meshtastic`
- `requests`

Установка Python-зависимостей:

```powershell
python -m pip install -r requirements.txt
```

Установка PlatformIO Core:

```powershell
python -m pip install platformio
```

## Быстрый старт

### Вариант 1. Прошить диагностический скетч

1. Подключить `Heltec V4` по USB.
2. Проверить номер порта, например `COM4`.
3. При необходимости поправить порт в `platformio.ini`.
4. Собрать прошивку:

```powershell
python -m platformio run
```

5. Залить на плату:

```powershell
python -m platformio run -t upload
```

6. Открыть монитор:

```powershell
python -m platformio device monitor
```

### Вариант 2. Оставить Meshtastic и проверить сеть

Если на плате уже установлен Meshtastic:

```powershell
python tools\meshtastic_diag.py --port COM4 --root-topic msh/RU/YAR
```

Скрипт:

- подключается к ноде
- выбирает подходящего соседа
- отправляет сообщение с `ACK`
- делает `traceroute`
- шлёт контрольный broadcast
- проверяет, появился ли он через ONEmesh API

## Команды диагностического скетча

После прошивки `src/main.cpp` доступны команды по serial:

- `1` / `2` / `3` - выбрать профиль
- `c` - CAD scan по всем профилям
- `r` - RX на активном профиле
- `a` - RX по всем профилям
- `g` - boosted RX на активном профиле
- `s` - sync-word sweep (`0x12` и `0x34`)
- `t` - отправить один пакет
- `b` - отправить серию пакетов
- `p` - power sweep от `2` до `20 dBm`
- `w` - непрерывная несущая `CW` на `8 секунд`
- `x` - полный цикл диагностики
- `i` - текущий статус
- `h` - помощь

### Что особенно полезно

- `c`
  Показывает, есть ли вообще LoRa-активность на нужных частотах.
- `p`
  Полезен, если рядом есть SDR, tinySA или вторая LoRa-нода.
- `w`
  Лучший тест, если нужно понять, излучает ли плата в эфир вообще.

## Восстановление Meshtastic

### 1. Залить factory firmware

```powershell
python C:\Users\Admin\.platformio\packages\tool-esptoolpy\esptool.py --chip esp32s3 --port COM4 --baud 921600 write_flash 0x0 firmware_restore\firmware-heltec-v4-2.7.18.fb3bf78.factory.bin
```

### 2. Применить готовый конфиг

```powershell
python -m meshtastic --port COM4 --configure meshtastic-onemesh-yar-current-2026-04-05.yaml
```

### 3. Проверить, что нода поднялась

```powershell
python -m meshtastic --port COM4 --info
```

## Текущее восстановленное состояние

На `2026-04-05` плата была возвращена в такое состояние:

- firmware: `2.7.18.fb3bf78`
- region: `RU`
- preset: `MEDIUM_FAST`
- hop limit: `7`
- `configOkToMqtt = true`
- `module_config.mqtt.enabled = false`

Актуальный YAML:

- `meshtastic-onemesh-yar-current-2026-04-05.yaml`

## Как выложить это на GitHub

Если локальный репозиторий уже создан, дальше обычно так:

```powershell
git remote add origin https://github.com/USERNAME/REPO.git
git branch -M main
git push -u origin main
```

## Примечания

- `onemesh-map.html`, `onemesh-mqtt.html` и `onemesh-v2.1.7.min.js` сохранены как локальные снапшоты во время диагностики.
- Если продолжать разбор проблемы, самый полезный внешний тест - смотреть `CW` или `power sweep` на SDR / tinySA / второй LoRa-ноде.
