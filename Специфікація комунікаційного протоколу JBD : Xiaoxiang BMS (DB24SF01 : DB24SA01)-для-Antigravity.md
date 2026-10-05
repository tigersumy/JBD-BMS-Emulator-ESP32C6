Нижче наведено структуровану технічну специфікацію протоколу для проєктування та реалізації прошивки/парсерів у середовищі Antigravity.

# **Специфікація комунікаційного протоколу JBD / Xiaoxiang BMS (DB24SF01 / DB24SA01)**

## **1\. Апаратний та транспортний рівень**

> * **Фізичні інтерфейси:** UART (TTL 3.3V/5V), RS485 (через трансивер MAX485/SP3485) або Bluetooth Low Energy (BLE).  
> * **Параметри послідовного порту (UART/RS485):**  
  * Швидкість (Baud Rate): 9600  
  * Біти даних: 8  
  * Парність: None  
  * Стоп-біти: 1  
  * Апаратне керування потоком: None  
> * **Таймаути та таймінги:**  
  * Рекомендований інтервал між опитуваннями: \\ge 1000\\text{ мс}.  
  * Мінімальна пауза між відправкою окремих запитів: 100\\text{–}200\\text{ мс}.  
  * Таймаут очікування відповіді: 500\\text{ мс}.  
> * **Порядок байтів (Endianness):** Big-Endian (MSB first) для всіх мультибайтних числових значень.

## **2\. Структура фрейму даних**

Кожен обмін складається з бінарного фрейму фіксованої структури з контрольною сумою.

### **2.1. Формат запиту від Master (ESP32/MCU) до BMS**

| Зміщення | Поле | Розмір (байти) | Опис / Значення |
| :---- | :---- | :---- | :---- |
| 0 | Стартовий байт | 1 | Завжди 0xDD |
| 1 | Тип операції | 1 | 0xA5 — Читання (Read), 0x5A — Запис (Write) |
| 2 | Команда / Регістр | 1 | Код запитуваного параметра (наприклад, 0x03, 0x04) |
| 3 | Довжина даних | 1 | Кількість байтів даних у запиті (0x00 при читанні) |
| 4 | Тіло даних | N | Байти налаштувань (відсутні при читанні, N=0) |
| 4+N | Контрольна сума | 2 | Двобайтова контрольна сума (MSB, LSB) |
| 6+N | Стоповий байт | 1 | Завжди 0x77 |

### **2.2. Формат відповіді від BMS до Master**

| Зміщення | Поле | Розмір (байти) | Опис / Значення |
| :---- | :---- | :---- | :---- |
| 0 | Стартовий байт | 1 | Завжди 0xDD |
| 1 | Команда / Регістр | 1 | Ехо ідентифікатора команди |
| 2 | Статус відповіді | 1 | 0x00 — Успіх (OK), 0x80 — Помилка |
| 3 | Довжина даних (L) | 1 | Кількість корисних байтів у відповіді |
| 4 .. 3+L | Дані (Payload) | L | Корисне навантаження |
| 4+L .. 5+L | Контрольна сума | 2 | Двобайтова сума (MSB, LSB) |
| 6+L | Стоповий байт | 1 | Завжди 0x77 |

## **3\. Алгоритм розрахунку контрольної суми (Checksum)**

Контрольна сума розраховується як доповнення до 0x10000 (або порозрядне інвертування \+ 1\) суми байтів корисного навантаження. Стартовий (0xDD) та стоповий (0x77) байти до підрахунку **не входять**.  
> Checksum \= 0x10000 \- \\sum (Bytes)

> * **Для запиту:** сумуються байти \[Команда\] \+ \[Довжина\] \+ \[Дані\].  
> * **Для відповіді:** сумуються байти \[Команда\] \+ \[Статус\] \+ \[Довжина\] \+ \[Дані...\].

### **Реалізація на C / C++**

`uint16_t jbd_calculate_checksum(const uint8_t *data, size_t length) {`  
    `uint16_t sum = 0;`  
    `for (size_t i = 0; i < length; ++i) {`  
        `sum += data[i];`  
    `}`  
    `return (uint16_t)(0x10000 - sum);`  
`}`

## **4\. Карта основних регістрів телеметрії**

### **4.1. Регістр 0x03 — Загальний стан системи (Basic System Info)**

> * **Запит (Hex):** DD A5 03 00 FF FD 77  
> * **Очікуваний розмір payload (L):** зазвичай 27\\text{–}31 байт (залежно від версії прошивки та кількості NTC).

#### **Розбір корисного навантаження (Payload Data Map):**

| Зміщення | Поле | Розмір | Тип / Формат | Опис та масштабування |
| :---- | :---- | :---- | :---- | :---- |
| 0..1 | Загальна напруга | 2 | uint16\_t | Одиниця: 10\\text{ мВ} (0.01\\text{ В}). Наприклад, 2652 \= 26.52\\text{ В}. |
| 2..3 | Струм | 2 | int16\_t | Знаковий. Одиниця: 10\\text{ мА} (0.01\\text{ А}). Позитивний \= заряд, негативний \= розряд. |
| 4..5 | Залишкова ємність | 2 | uint16\_t | Одиниця: 10\\text{ мА·год} (0.01\\text{ А·год}). |
| 6..7 | Номінальна ємність | 2 | uint16\_t | Одиниця: 10\\text{ мА·год} (0.01\\text{ А·год}). |
| 8..9 | Кількість циклів | 2 | uint16\_t | Кількість повних циклів перезаряду. |
| 10..11 | Дата виробництва | 2 | uint16\_t | Бітове поле: рік (val \>\> 9\) \+ 2000, місяць (val \>\> 5\) & 0x0F, день val & 0x1F. |
| 12..15 | Балансування (Low/High) | 4 | uint32\_t | Бітова маска активного балансування. Біт 0 \= комірка 1, Біт 1 \= комірка 2 тощо. |
| 16..17 | Статус захистів (Errors) | 2 | uint16\_t | Бітова маска тригерів аварій/захистів. |
| 18 | Версія ПЗ | 1 | uint8\_t | Номер версії софту. |
| 19 | Залишок заряду (SOC) | 1 | uint8\_t | Одиниця: \\%. Значення від 0 до 100\. |
| 20 | Стан MOSFET ключів | 1 | uint8\_t | Біт 0: Charge MOS (1 \= \\text{ON}, 0 \= \\text{OFF}); Біт 1: Discharge MOS (1 \= \\text{ON}, 0 \= \\text{OFF}). |
| 21 | Кількість комірок | 1 | uint8\_t | Кількість активованих послідовних елементів (для 8S \= 0x08). |
| 22 | Кількість NTC | 1 | uint8\_t | Кількість температурних сенсорів (зазвичай 2 або 3). |
| 23.. | Температури сенсорів | K \\times 2 | uint16\_t | Кельвіни з множником 10: T(^\\circ\\text{C}) \= \\frac{Value \- 2731}{10.0}. |

#### **Бітова маска помилок і захистів (Зміщення 16..17):**

> * **Біт 0:** Перевищення напруги однієї з комірок (Cell Overvoltage)  
> * **Біт 1:** Зниження напруги однієї з комірок (Cell Undervoltage)  
> * **Біт 2:** Перевищення загальної напруги батареї (Pack Overvoltage)  
> * **Біт 3:** Зниження загальної напруги батареї (Pack Undervoltage)  
> * **Біт 4:** Перегрів під час заряду (Charge Overtemperature)  
> * **Біт 5:** Переохолодження під час заряду (Charge Undertemperature)  
> * **Біт 6:** Перегрів під час розряду (Discharge Overtemperature)  
> * **Біт 7:** Переохолодження під час розряду (Discharge Undertemperature)  
> * **Біт 8:** Перевантаження по струму заряду (Charge Overcurrent)  
> * **Біт 9:** Перевантаження по струму розряду (Discharge Overcurrent)  
> * **Біт 10:** Коротке замикання (Short Circuit)  
> * **Біт 11:** Помилка мікросхеми AFE/фронтенду (IC Error)  
> * **Біт 12:** Пошкодження ключів MOS (MOS Software Lock / Failure)

### **4.2. Регістр 0x04 — Напруги окремих комірок (Cell Voltages)**

> * **Запит (Hex):** DD A5 04 00 FF FC 77  
> * **Формат Payload:**  
  * Кожна комірка займає 2 байти (uint16\_t, Big-Endian).  
  * Значення вимірюється в **мілівольтах (\\text{мВ})** безпосередньо.  
  * Кількість значень: L / 2\. Для конфігурації 8S повертається 16 байт корисного навантаження (8 комірок).

### **4.3. Регістр 0x05 — Назва пристрою / Ідентифікатор прошивки**

> * **Запит (Hex):** DD A5 05 00 FF FB 77  
> * **Формат Payload:**  
  * ASCII-рядок довільної довжини (зазвичай L байтів тексту).  
  * Містить найменування моделі або серійний код плати.

## **5\. Приклад кінцевого автомата (FSM) для розбору пакетів на C++**

`#pragma once`  
`#include <Arduino.h>`

`enum class JbdParserState {`  
    `WAIT_START,`  
    `READ_CMD,`  
    `READ_STATUS,`  
    `READ_LEN,`  
    `READ_DATA,`  
    `READ_CHECKSUM_H,`  
    `READ_CHECKSUM_L,`  
    `READ_STOP`  
`};`

`class JbdProtocolParser {`  
`public:`  
    `void feedByte(uint8_t b) {`  
        `switch (state_) {`  
            `case JbdParserState::WAIT_START:`  
                `if (b == 0xDD) {`  
                    `state_ = JbdParserState::READ_CMD;`  
                `}`  
                `break;`  
            `case JbdParserState::READ_CMD:`  
                `cmd_ = b;`  
                `state_ = JbdParserState::READ_STATUS;`  
                `break;`  
            `case JbdParserState::READ_STATUS:`  
                `status_ = b;`  
                `state_ = JbdParserState::READ_LEN;`  
                `break;`  
            `case JbdParserState::READ_LEN:`  
                `len_ = b;`  
                `idx_ = 0;`  
                `if (len_ > 64) { // Перевищення допустимого розміру буфера`  
                    `reset();`  
                `} else if (len_ == 0) {`  
                    `state_ = JbdParserState::READ_CHECKSUM_H;`  
                `} else {`  
                    `state_ = JbdParserState::READ_DATA;`  
                `}`  
                `break;`  
            `case JbdParserState::READ_DATA:`  
                `buffer_[idx_++] = b;`  
                `if (idx_ >= len_) {`  
                    `state_ = JbdParserState::READ_CHECKSUM_H;`  
                `}`  
                `break;`  
            `case JbdParserState::READ_CHECKSUM_H:`  
                `crc_recv_ = (uint16_t)b << 8;`  
                `state_ = JbdParserState::READ_CHECKSUM_L;`  
                `break;`  
            `case JbdParserState::READ_CHECKSUM_L:`  
                `crc_recv_ |= b;`  
                `state_ = JbdParserState::READ_STOP;`  
                `break;`  
            `case JbdParserState::READ_STOP:`  
                `if (b == 0x77) {`  
                    `if (verifyChecksum()) {`  
                        `processPayload(cmd_, status_, buffer_, len_);`  
                    `}`  
                `}`  
                `reset();`  
                `break;`  
        `}`  
    `}`

`private:`  
    `JbdParserState state_ = JbdParserState::WAIT_START;`  
    `uint8_t cmd_ = 0;`  
    `uint8_t status_ = 0;`  
    `uint8_t len_ = 0;`  
    `uint8_t idx_ = 0;`  
    `uint16_t crc_recv_ = 0;`  
    `uint8_t buffer_[64];`

    `void reset() {`  
        `state_ = JbdParserState::WAIT_START;`  
        `idx_ = 0;`  
    `}`

    `bool verifyChecksum() {`  
        `uint16_t sum = cmd_ + status_ + len_;`  
        `for (uint8_t i = 0; i < len_; ++i) {`  
            `sum += buffer_[i];`  
        `}`  
        `uint16_t calculated = (uint16_t)(0x10000 - sum);`  
        `return calculated == crc_recv_;`  
    `}`

    `void processPayload(uint8_t cmd, uint8_t status, const uint8_t *data, uint8_t len) {`  
        `if (status != 0x00) return;`

        `if (cmd == 0x04) {`  
            `// Розбір напруг комірок (8S = 16 байтів)`  
            `for (uint8_t i = 0; i < len / 2; ++i) {`  
                `uint16_t cell_mv = ((uint16_t)data[i * 2] << 8) | data[i * 2 + 1];`  
                `// cell_mv містить значення напруги в мілівольтах`  
            `}`  
        `} else if (cmd == 0x03) {`  
            `// Розбір основного блоку телеметрії`  
            `uint16_t total_v_raw = ((uint16_t)data[0] << 8) | data[1];`  
            `float total_v = total_v_raw * 0.01f;`

            `int16_t current_raw = ((int16_t)data[2] << 8) | data[3];`  
            `float current = current_raw * 0.01f;`

            `uint8_t soc = data[19];`  
            `uint8_t mos_state = data[20];`  
            `// Подальший експорт значень (MQTT, Modbus або HomeKit)...`  
        `}`  
    `}`  
`};`  
