import serial
import serial.tools.list_ports
import threading
import time
import sys

class ESP32BluetoothSerial:
    def __init__(self):
        self.serial_conn = None
        self.connected = False
        self.receive_callback = None
        
    def list_ports(self):
        """Список доступных COM портов"""
        try:
            ports = serial.tools.list_ports.comports()
            print("Доступные COM порты:")
            if not ports:
                print("  Порты не найдены")
                return []
                
            for port in ports:
                print(f"  {port.device} - {port.description}")
            return ports
        except Exception as e:
            print(f"Ошибка при получении списка портов: {e}")
            return []
        
    def connect(self, port_name=None, baudrate=115200):
        """Подключение к COM порту"""
        try:
            if port_name:
                port = port_name
            else:
                # Автопоиск порта с ESP32
                ports = self.list_ports()
                port = None
                for p in ports:
                    desc_lower = p.description.lower() if p.description else ""
                    if 'bluetooth' in desc_lower or 'esp' in desc_lower or 'serial' in desc_lower:
                        port = p.device
                        print(f"Найден подходящий порт: {port} - {p.description}")
                        break
                
                if not port and ports:
                    port = ports[0].device
                    print(f"Автоматически выбран порт: {port}")
            
            if not port:
                print("COM порты не найдены")
                return False
            
            print(f"Подключение к {port} с Baudrate {baudrate}...")
            self.serial_conn = serial.Serial(
                port=port,
                baudrate=baudrate,
                timeout=1,
                write_timeout=1,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE
            )
            
            # Ожидание инициализации
            time.sleep(2)
            self.connected = True
            print("Подключение установлено!")
            
            # Очистка буфера
            self.serial_conn.reset_input_buffer()
            self.serial_conn.reset_output_buffer()
            
            return True
            
        except serial.SerialException as e:
            print(f"Ошибка подключения к {port_name}: {e}")
            return False
        except Exception as e:
            print(f"Неожиданная ошибка: {e}")
            return False
    
    def set_receive_callback(self, callback):
        """Установка функции обратного вызова для приема данных"""
        self.receive_callback = callback
    
    def start_receiving(self):
        """Запуск потока для приема данных"""
        if not self.connected or not self.serial_conn:
            print("Не подключено к устройству")
            return
        
        def receive_thread():
            buffer = ""
            while self.connected:
                try:
                    if self.serial_conn and self.serial_conn.in_waiting > 0:
                        # Читаем все доступные данные
                        data = self.serial_conn.read(self.serial_conn.in_waiting).decode('utf-8', errors='ignore')
                        buffer += data
                        
                        # Обрабатываем завершенные строки
                        while '\n' in buffer:
                            line, buffer = buffer.split('\n', 1)
                            line = line.strip()
                            if line:
                                print(f"Получено: {line}")
                                if self.receive_callback:
                                    self.receive_callback(line)
                    
                    time.sleep(0.01)  # Небольшая задержка для уменьшения нагрузки на CPU
                    
                except Exception as e:
                    if self.connected:
                        print(f"Ошибка приема: {e}")
                    break
        
        thread = threading.Thread(target=receive_thread)
        thread.daemon = True
        thread.start()
        print("Поток приема данных запущен")
    
    def send_data(self, data):
        """Отправка данных"""
        if not self.connected or not self.serial_conn:
            print("Не подключено к устройству")
            return False
        
        try:
            if not data.endswith('\n'):
                data += '\n'
                
            self.serial_conn.write(data.encode('utf-8'))
            self.serial_conn.flush()  # Убедимся, что данные отправлены
            print(f"Отправлено: {data.strip()}")
            return True
            
        except Exception as e:
            print(f"Ошибка отправки: {e}")
            return False
    
    def disconnect(self):
        """Отключение от устройства"""
        self.connected = False
        if self.serial_conn:
            try:
                self.serial_conn.close()
                print("Отключено от устройства")
            except:
                pass

def main():
    # Создание экземпляра Bluetooth соединения
    esp32 = ESP32BluetoothSerial()
    
    # Функция для обработки входящих сообщений
    def handle_received_data(data):
        print(f"[Callback] Получены данные: {data}")
    
    esp32.set_receive_callback(handle_received_data)
    
    print("=== ESP32 Bluetooth Communication ===")
    
    # Показать доступные порты
    esp32.list_ports()
    
    # Подключение к устройству
    print("\nПопытка автоматического подключения...")
    if not esp32.connect():
        print("\nАвтоматическое подключение не удалось.")
        print("Пожалуйста, введите COM порт вручную:")
        
        while True:
            port_name = input("COM порт (например, COM3): ").strip()
            if not port_name:
                print("Выход...")
                return
                
            if esp32.connect(port_name):
                break
            else:
                print("Попробуйте другой порт или нажмите Enter для выхода")
    
    # Запуск приема данных в отдельном потоке
    esp32.start_receiving()
    
    try:
        print("\n" + "="*50)
        print("Управление:")
        print("  - Введите сообщение для отправки на ESP32")
        print("  - Введите 'quit' для выхода")
        print("  - Введите 'ports' для списка портов")
        print("  - Введите 'status' для проверки статуса")
        print("  - Нажмите Ctrl+C для экстренного выхода")
        print("="*50 + "\n")
        
        while True:
            try:
                user_input = input("> ").strip()
                
                if user_input.lower() == 'quit':
                    break
                elif user_input.lower() == 'ports':
                    esp32.list_ports()
                elif user_input.lower() == 'status':
                    status = "Подключено" if esp32.connected else "Не подключено"
                    print(f"Статус подключения: {status}")
                elif user_input:
                    if not esp32.send_data(user_input):
                        print("Ошибка отправки, проверьте подключение")
                        break
                        
            except KeyboardInterrupt:
                print("\nПрервано пользователем (Ctrl+C)")
                break
            except Exception as e:
                print(f"Ошибка ввода: {e}")
                break
                
    except Exception as e:
        print(f"Неожиданная ошибка: {e}")
    finally:
        esp32.disconnect()
        print("Программа завершена")

if __name__ == "__main__":
    main()