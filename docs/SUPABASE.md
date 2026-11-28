# Данные в Supabase

Плата и симулятор отправляют состояние комнат в базу Supabase, к уже существующей
таблице `rooms`. Существующие таблицы (`rooms`, `bookings`, `booking_log`, ...) не меняются.

```
плата (Live Lab) ─┐
                  ├─ CP> строки ─> bridge/cp_bridge.py ─> Supabase: room_presence, presence_log
fleet_sim.py ─────┘   (симуляция остальных комнат)              └─> view room_status для сайта
```

## Как связаны комнаты и датчики

- Датчик называет свою комнату по `rooms.board_key` (например `Z-3-04a`).
  Мост сам находит соответствующий `rooms.id` (`Z.3.04a`).
- **Реальная плата одна.** Ей задаётся одна комната при сборке:
  `apply_to_example.sh <пример> --room Z-3-04a`.
- **Остальные комнаты** симулирует `bridge/fleet_sim.py`: это та же логика на C
  (`build/cp_sim`), по одному процессу на комнату. Список комнат берётся из базы.
  У симулированных комнат `mode = 'sim'`, у платы `mode = 'camera'`.

## 1. Создать таблицы (один раз)

Supabase → **SQL Editor** → вставьте весь файл
[`supabase/001_room_presence.sql`](../supabase/001_room_presence.sql) → **Run**.
Повторный запуск безопасен.

Что появится:

| Объект | Что хранит |
|---|---|
| `room_presence` | последнее состояние каждой комнаты (одна строка на комнату) |
| `presence_log` | история изменений, без heartbeat-сообщений раз в 5 с |
| `room_status` (view) | все комнаты + готовый `status`: `occupied`, `empty`, `release_due`, `sensor_unavailable`, `no_sensor` |

`release_due` означает, что в комнате никого нет 45 минут (2700 с) и датчик исправен.
Если датчик молчит больше 15 с или выдаёт `stale`, статус будет `sensor_unavailable`:
сломанная камера не считается пустой комнатой.

## 2. Ключ

Supabase → **Project Settings → API Keys** → ключ **service_role** (secret).
Он даёт полный доступ к базе. **Не вставляйте его в чат, в код или в GitHub.**
Задайте его в своём терминале:

```bash
export SUPABASE_KEY='...вставьте ключ...'
```

Мост только пишет в `room_presence` и `presence_log` и читает `rooms`.

## 3. Запуск

```bash
make                       # собирает build/cp_sim
export SUPABASE_URL=https://<project>.supabase.co
```

Все комнаты симулированы, кроме комнаты платы (`--speed 60`: 1 секунда = 1 минута,
правило 45 минут срабатывает за 45 секунд):
```bash
python3 bridge/fleet_sim.py --supabase $SUPABASE_URL --exclude Z-3-04a --speed 60 | python3 bridge/cp_bridge.py --supabase $SUPABASE_URL
```

Данные платы передаёт «читалка» Live Lab Console Output (HANDOFF §6), которая
пишет в мост:
```bash
<читалка> | python3 bridge/cp_bridge.py --supabase $SUPABASE_URL --url http://localhost:8080/api/sensor
```
`--url` дополнительно отправляет данные в локальный приёмник, это можно не указывать.

## 4. Проверить в Supabase

```sql
select room_id, status, people_count, empty_for_s, mode, updated_at
from room_status order by room_id;
```

## Как сайт читает данные

Сайт читает `room_status` анонимным ключом (`anon`): на чтение открыта политика RLS,
писать может только мост. Освобождение брони (статус `release_due`) делает сайт или
серверная функция. Датчики только сообщают состояние.
