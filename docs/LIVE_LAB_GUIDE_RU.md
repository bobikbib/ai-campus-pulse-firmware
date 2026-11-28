# Инструкция на вечер: Infineon Live Lab

Работаем вместе с Claude: он видит экран Live Lab и подсказывает. Если что-то
пошло не так, просто напишите ему, что видите.

---

## Подготовка на новом ноутбуке (один раз)

1. Откройте Claude Code в папке, куда хотите положить проект, и скажите:
   «склонируй https://github.com/bobikbib/ai-campus-pulse-firmware.git и прочитай CLAUDE.md».
2. Claude прочитает `CLAUDE.md` и `docs/HANDOFF.md` и узнает всё, что сделано до этого.
3. Скажите Claude открыть Live Lab в его браузере:
   https://infineon-live-lab.tenxerlabs.com/app/psoc-edge-e84
4. **Войдите в аккаунт сами.** Пароли Claude не вводит.

---

# СЕССИЯ 1: разведка (~1 час)

**Цель:** проверить, что пример Infineon собирается и прошивается, и что мы
правильно считаем людей. Нашу прошивку в этой сессии **не ставим**.

### Шаг 1. Открыть рабочее место (5 мин)
1. Слева в Live Lab откройте **Developer Workspace (ModusToolbox™)**. Откроется
   удалённый рабочий стол.
2. Откройте **терминал** (`modus-shell` на Windows, Terminal на Linux).
3. Проверьте инструменты:
   ```bash
   git --version
   ```
   ```bash
   arm-none-eabi-gcc --version
   ```
   📝 Запишите версии. Если какой-то команды нет, скажите Claude.

### Шаг 2. Скачать наш проект (1 мин)
```bash
git clone https://github.com/bobikbib/ai-campus-pulse-firmware.git
```
✅ Должно скачаться **без пароля**.

### Шаг 3. Скачать пример Infineon (5–10 мин)
В той же папке, что и шаг 2:
```bash
git clone https://github.com/Infineon/mtb-example-psoc-edge-ml-aihub-deploy-vision.git
```
```bash
cd mtb-example-psoc-edge-ml-aihub-deploy-vision
```
```bash
make getlibs
```
📝 Запишите, сколько это заняло.

### Шаг 4. Собрать пример БЕЗ изменений (10–15 мин)
```bash
make build -j8
```
- ✅ В конце будет **таблица памяти**, сделайте скриншот.
- ❌ Если ошибка, скопируйте **первую** строку с `error`.
- ⚠️ Если ошибка про **TARGET / BSP**, попробуйте:
  ```bash
  make build -j8 TARGET=KIT_PSE84_EVAL_EPC4
  ```

### Шаг 5. Прошить и запустить (5 мин)
```bash
make program
```
(если не сработает, в Live Lab может быть своя кнопка прошивки)

1. В **Console Output** должно появиться `PSOC Edge MCU: AI Hub deploy vision`.
2. Включите камеру: **Webcam Access → Enable**, разрешите доступ в браузере.
3. 📝 Найдите в консоли `Vendor Id = 0x....` и `Product Id = 0x....`, запишите.
4. Посмотрите на дисплей платы (EVK Display Livestream):
   - ✅ видно вас, закрашенного синей маской → **шаг 6**;
   - ❌ «camera not supported» → **план Б** (ниже).

### Шаг 6. Проверить подсчёт людей (10 мин)
Скрипт сам вставит отладочную строку:
```bash
bash ../ai-campus-pulse-firmware/tools/apply_to_example.sh . --debug
```
```bash
make build -j8 && make program
```
В Console Output пойдут строки `DBG people=1 count_idx=... start=... end=...`.

| Что делаем | Ожидаем `people` | Получили |
|---|---|---|
| Никого в кадре | 0 | |
| Вы в кадре | 1 | |
| Вы и друг | 2 | |
| Вы отвернулись | 1 | |

📝 Сколько строк DBG в секунду (частота кадров)?

### Шаг 7. Откатить изменения
```bash
git checkout proj_cm55
```

### Шаг 8. Итоги
Передайте Claude: версии, время сборки и прошивки, таблицу памяти, VID/PID камеры,
таблицу из шага 6, частоту кадров, ошибки. Claude запишет их в
`docs/SESSION1_CHECKLIST.md` (раздел Results) и подправит код к сессии 2.

---

# ПЛАН Б (камера не подошла или пример не собрался)

Сначала выйдите в общую папку:
```bash
cd ..
```
```bash
git clone https://github.com/Infineon/mtb-example-psoc-edge-ml-face-id.git
```
```bash
cd mtb-example-psoc-edge-ml-face-id && make getlibs
```
```bash
make build -j8 && make program
```
Если на дисплее видны лица, ставим нашу прошивку сразу в этот пример:
```bash
bash ../ai-campus-pulse-firmware/tools/apply_to_example.sh .
```
```bash
make build -j8 && make program
```
⚠️ Этот пример считает **лица**: отвернувшегося человека он не видит.

**Если не работает и это:** на демо показываем встроенный Face ID на плате
(«ИИ работает на плате»), а логику броней — через симулятор на ноутбуке.

---

# СЕССИЯ 2: ставим нашу прошивку

### Шаг 1. Обновить наш проект
```bash
cd ai-campus-pulse-firmware && git pull && cd ..
```

### Шаг 2. Вставить наш код в пример
```bash
cd mtb-example-psoc-edge-ml-aihub-deploy-vision
```
```bash
git checkout proj_cm55
```
```bash
bash ../ai-campus-pulse-firmware/tools/apply_to_example.sh . --room Z-3-04a
```
`--room` это `board_key` комнаты из таблицы `rooms` в Supabase: плата будет «стоять» в этой комнате.
✅ Скрипт пишет `copied sources...`, `patched...`, `done`.
❌ `ERROR: anchor ... not found` → покажите Claude текст ошибки.

### Шаг 3. Собрать и прошить
```bash
make build -j8 && make program
```

### Шаг 4. Проверить, что наша прошивка работает
В Console Output должно быть:
1. `CP>{"v":1,"type":"hello","fw":"0.1.0"}`
2. `CP>{..."type":"presence"...}` раз в 5 секунд
3. через 30 с `CP>{..."type":"diag","stack_free_bytes":...}`. 📝 Число должно быть **≥ 1500**.

### Шаг 5. Сценарий «призрачной брони»
| Что делаем | Что должно быть в строке `CP>` |
|---|---|
| Сядьте перед камерой | через ~1 с `"occupied":true`, `"people_count":1` |
| Отойдите из кадра | 10 с ещё `true`, затем `"occupied":false` |
| Стойте вне кадра | `"empty_for_s"` растёт: 10, 15, 20… |
| Вернитесь | `"occupied":true`, `"empty_for_s":0` |

✅ Если всё так, плата готова к демо.

### Шаг 6. Данные на ноутбук
Строки с платы видны только в Console Output на странице Live Lab. Попросите Claude
сделать «читалку» по `docs/HANDOFF.md`, раздел 6: она забирает строки `CP>` со
страницы и пересылает их в приёмник на ноутбуке.

На ноутбуке в отдельном терминале запустите приёмник:
```bash
python3 bridge/receiver.py
```
Данные будут видны на **http://localhost:8080** и сохранятся в `data/readings.jsonl`.

Чтобы данные шли в **Supabase** (и плата, и симуляция остальных комнат), смотрите
[SUPABASE.md](SUPABASE.md).

---

# Если что-то пошло не так

| Проблема | Что делать |
|---|---|
| Live Lab не пускает | проверьте время слота в календаре Live Lab |
| Сборка падает | покажите Claude **первую** строку с `error` |
| Плата «зависла» | кнопка **Refresh** у видео или перезапуск через Live Lab |
| В консоли пусто | откройте **Console Output**, а не Progress Log |
| Время слота кончается | сохраните записи, остальное доделаем во второй сессии |
