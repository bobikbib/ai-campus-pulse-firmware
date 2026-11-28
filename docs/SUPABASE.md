# Данные в Supabase (база сайта друга)

База и сайт живут в репозитории друга `Room_booking_system`
(`backend/supabase/migrations`). Там уже есть функция **`ingest_board`**, сделанная
ровно под наш формат `CP>` (`room`, `people_count`, `occupied`, `empty_for_s`,
`sensor`, `uptime_s`). Наш мост просто вызывает её.

```
плата (Live Lab) ─┐
                  ├─ CP> строки ─> bridge/cp_bridge.py ──POST /rest/v1/rpc/ingest_board──> Supabase
fleet_sim.py ─────┘                                                          │
 (остальные комнаты)                                room_state, history, v_room_status (сайт)
                                                    run_booking_jobs: освобождает «призрачные» брони
```

Всё остальное делает сама база:
- статус комнаты (`v_room_status`: free, reserved, occupied, ghost, offline);
- правило 45 минут и предупреждение за 10 минут;
- игнорирование «датчик молчит / stale». Сломанная камера не считается пустой комнатой.

## Комнаты

- Датчик называет комнату по `rooms.board_key`, это id с `-` вместо `.`
  (`Z.3.03` → `Z-3-03`).
- Реальная плата одна, её комната задаётся при сборке:
  `apply_to_example.sh <пример> --room Z-3-04a`.
- Остальные комнаты симулирует `bridge/fleet_sim.py`. Список комнат он берёт из базы.

## Ключ

Хватает публичного ключа **publishable / anon**, того же, что в `app/js/config.js`
у друга. Секретный `service_role` **не нужен**.

```bash
export SUPABASE_KEY='sb_publishable_...'
```

## Запуск

```bash
make
export SUPABASE_URL=https://<project>.supabase.co
python3 bridge/fleet_sim.py --supabase $SUPABASE_URL --exclude Z-3-04a | python3 bridge/cp_bridge.py --supabase $SUPABASE_URL
```

⚠️ **Симуляция влияет на настоящие брони.** База не отличает симуляцию от платы.
Если в симулированной комнате идёт бронь, а симулятор «увёл» людей, бронь
освободится через 45 минут. С `--speed 60` это случится через 45 *секунд*.
Поэтому:
- для демо с ускорением симулируйте только нужные комнаты: `--rooms Z-3-03,Z-3-04b`;
- комнату, которую бронируют вручную для проверки, исключите: `--exclude Z-3-04a,Z-3-03`.

## Проверить

```sql
select room_id, status, people_count, empty_for_sec, releases_in_sec, sensor, last_event_at
from v_room_status order by room_id;
```
