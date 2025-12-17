// Основной скрипт приложения для работы с COM-портами через Web Serial API

class COMPortManager {
    constructor() {
        this.port = null;
        this.reader = null;
        this.writer = null;
        this.isConnected = false;
        this.isPaused = false;
        this.receivedBytes = 0;
        this.buffer = '';
        this.dataBuffer = '';
        
        // Проверка поддержки Web Serial API
        this.checkCompatibility();
        
        // Инициализация приложения
        this.init();
    }
    
    // Проверка поддержки браузером Web Serial API
    checkCompatibility() {
        if (!('serial' in navigator)) {
            document.getElementById('compatibilityWarning').classList.remove('hidden');
            document.getElementById('connectBtn').disabled = true;
            this.showNotification('Ваш браузер не поддерживает Web Serial API. Используйте Chrome 89+ или Edge 79+', 'error');
            return false;
        }
        return true;
    }
    
    // Инициализация приложения
    init() {
        // Назначение обработчиков событий
        document.getElementById('connectBtn').addEventListener('click', () => this.connect());
        document.getElementById('disconnectBtn').addEventListener('click', () => this.disconnect());
        document.getElementById('refreshPorts').addEventListener('click', () => this.updatePortList());
        document.getElementById('clearData').addEventListener('click', () => this.clearData());
        document.getElementById('pauseResume').addEventListener('click', () => this.togglePause());
        document.getElementById('saveData').addEventListener('click', () => this.saveData());
        document.getElementById('sendData').addEventListener('click', () => this.sendData());
        
        // Обновление списка портов при загрузке
        this.updatePortList();
    }
    
    // Обновление списка доступных портов
    async updatePortList() {
        try {
            // Получение списка портов
            const ports = await navigator.serial.getPorts();
            this.displayPorts(ports);
            
            // Запрос разрешения на доступ к портам (откроет диалог выбора порта)
            // Это нужно для получения новых портов, которые еще не были предоставлены
            document.getElementById('portList').innerHTML = '<div class="port-item">Нажмите "Обновить список портов" и выберите порт в диалоговом окне</div>';
            
            this.showNotification('Для выбора порта используйте диалоговое окно при подключении', 'info');
        } catch (error) {
            console.error('Ошибка при обновлении списка портов:', error);
            this.showNotification('Ошибка при обновлении списка портов', 'error');
        }
    }
    
    // Отображение списка портов
    displayPorts(ports) {
        const portList = document.getElementById('portList');
        
        if (ports.length === 0) {
            portList.innerHTML = '<div class="port-item">COM-порты не обнаружены</div>';
            return;
        }
        
        let html = '';
        ports.forEach((port, index) => {
            const portInfo = port.getInfo();
            html += `
                <div class="port-item" data-index="${index}">
                    <strong>Порт ${index + 1}</strong><br>
                    <small>Vendor: ${portInfo.usbVendorId || 'N/A'}, Product: ${portInfo.usbProductId || 'N/A'}</small>
                </div>
            `;
        });
        
        portList.innerHTML = html;
        
        // Добавление обработчиков выбора порта
        const portItems = portList.querySelectorAll('.port-item');
        portItems.forEach(item => {
            item.addEventListener('click', (e) => {
                // Снятие выделения со всех портов
                portItems.forEach(i => i.classList.remove('selected'));
                // Выделение выбранного порта
                e.currentTarget.classList.add('selected');
            });
        });
    }
    
    // Подключение к выбранному порту
    async connect() {
        try {
            // Запрос выбора порта у пользователя
            this.port = await navigator.serial.requestPort();
            
            // Получение параметров подключения
            const baudRate = parseInt(document.getElementById('baudRate').value);
            const dataBits = parseInt(document.getElementById('dataBits').value);
            const stopBits = parseInt(document.getElementById('stopBits').value);
            const parity = document.getElementById('parity').value;
            
            // Настройка параметров порта
            await this.port.open({
                baudRate: baudRate,
                dataBits: dataBits,
                stopBits: stopBits,
                parity: parity,
                flowControl: 'none'
            });
            
            this.isConnected = true;
            this.updateUI();
            this.showNotification(`Подключено к COM-порту (${baudRate} бод)`, 'success');
            
            // Запуск чтения данных
            this.readData();
            
        } catch (error) {
            console.error('Ошибка подключения:', error);
            this.showNotification(`Ошибка подключения: ${error.message}`, 'error');
        }
    }
    
    // Отключение от порта
    async disconnect() {
        try {
            if (this.reader) {
                this.reader.cancel();
            }
            
            if (this.writer) {
                await this.writer.close();
            }
            
            if (this.port) {
                await this.port.close();
            }
            
            this.isConnected = false;
            this.updateUI();
            this.showNotification('Отключено от COM-порта', 'info');
            
        } catch (error) {
            console.error('Ошибка отключения:', error);
            this.showNotification('Ошибка при отключении', 'error');
        }
    }
    
    // Чтение данных из порта
    async readData() {
        const textDecoder = new TextDecoder();
        
        try {
            while (this.port.readable && this.isConnected) {
                this.reader = this.port.readable.getReader();
                
                try {
                    while (true) {
                        const { value, done } = await this.reader.read();
                        
                        if (done) {
                            break;
                        }
                        
                        if (value && !this.isPaused) {
                            this.processData(value, textDecoder);
                        }
                    }
                } catch (error) {
                    console.error('Ошибка чтения:', error);
                } finally {
                    this.reader.releaseLock();
                }
            }
        } catch (error) {
            console.error('Ошибка в цикле чтения:', error);
        }
    }
    
    // Обработка полученных данных
    processData(data, textDecoder) {
        // Обновление счетчика байт
        this.receivedBytes += data.length;
        document.getElementById('bytesReceived').textContent = this.receivedBytes;
        
        // Декодирование данных
        let decodedData = '';
        
        // Попытка декодировать как текст
        try {
            decodedData = textDecoder.decode(data);
        } catch (e) {
            // Если не удалось декодировать как текст, отображаем hex
            decodedData = this.arrayBufferToHex(data);
        }
        
        // Добавление данных в буфер
        this.buffer += decodedData;
        this.dataBuffer += decodedData;
        
        // Отображение данных
        this.displayData();
    }
    
    // Преобразование ArrayBuffer в hex строку
    arrayBufferToHex(buffer) {
        return Array.from(new Uint8Array(buffer))
            .map(b => b.toString(16).padStart(2, '0'))
            .join(' ');
    }
    
    // Отображение данных
    displayData() {
        const dataDisplay = document.getElementById('dataDisplay');
        
        // Ограничение размера отображаемых данных (для производительности)
        if (this.buffer.length > 10000) {
            this.buffer = this.buffer.slice(-10000);
        }
        
        dataDisplay.textContent = this.buffer;
        
        // Автопрокрутка к новым данным
        dataDisplay.scrollTop = dataDisplay.scrollHeight;
    }
    
    // Отправка данных в порт
    async sendData() {
        if (!this.isConnected || !this.port) {
            this.showNotification('Сначала подключитесь к порту', 'error');
            return;
        }
        
        const dataToSend = document.getElementById('dataToSend').value;
        
        if (!dataToSend) {
            this.showNotification('Введите данные для отправки', 'error');
            return;
        }
        
        try {
            // Создание writer для отправки данных
            const textEncoder = new TextEncoder();
            this.writer = this.port.writable.getWriter();
            
            // Отправка данных
            await this.writer.write(textEncoder.encode(dataToSend + '\n'));
            
            // Освобождение writer
            this.writer.releaseLock();
            
            this.showNotification(`Отправлено: ${dataToSend}`, 'success');
            document.getElementById('dataToSend').value = '';
            
        } catch (error) {
            console.error('Ошибка отправки:', error);
            this.showNotification('Ошибка отправки данных', 'error');
        }
    }
    
    // Очистка данных
    clearData() {
        this.buffer = '';
        this.dataBuffer = '';
        this.receivedBytes = 0;
        document.getElementById('bytesReceived').textContent = '0';
        document.getElementById('dataDisplay').textContent = '';
        this.showNotification('Данные очищены', 'info');
    }
    
    // Переключение паузы/возобновления
    togglePause() {
        this.isPaused = !this.isPaused;
        const pauseBtn = document.getElementById('pauseResume');
        
        if (this.isPaused) {
            pauseBtn.textContent = 'Возобновить';
            document.getElementById('currentStatus').textContent = 'Приостановлено';
            this.showNotification('Чтение приостановлено', 'info');
        } else {
            pauseBtn.textContent = 'Приостановить';
            document.getElementById('currentStatus').textContent = 'Активно';
            this.showNotification('Чтение возобновлено', 'success');
        }
    }
    
    // Сохранение данных в файл
    saveData() {
        if (!this.dataBuffer) {
            this.showNotification('Нет данных для сохранения', 'error');
            return;
        }
        
        try {
            const blob = new Blob([this.dataBuffer], { type: 'text/plain' });
            const url = URL.createObjectURL(blob);
            const a = document.createElement('a');
            
            a.href = url;
            a.download = `com-port-data-${new Date().toISOString().slice(0, 19).replace(/:/g, '-')}.txt`;
            document.body.appendChild(a);
            a.click();
            document.body.removeChild(a);
            URL.revokeObjectURL(url);
            
            this.showNotification('Данные сохранены', 'success');
            
        } catch (error) {
            console.error('Ошибка сохранения:', error);
            this.showNotification('Ошибка сохранения данных', 'error');
        }
    }
    
    // Обновление интерфейса
    updateUI() {
        const connectBtn = document.getElementById('connectBtn');
        const disconnectBtn = document.getElementById('disconnectBtn');
        const statusDiv = document.getElementById('connectionStatus');
        const currentStatus = document.getElementById('currentStatus');
        
        if (this.isConnected) {
            connectBtn.disabled = true;
            disconnectBtn.disabled = false;
            statusDiv.textContent = 'Подключено';
            statusDiv.className = 'connection-status connected';
            currentStatus.textContent = 'Активно';
        } else {
            connectBtn.disabled = false;
            disconnectBtn.disabled = true;
            statusDiv.textContent = 'Не подключено';
            statusDiv.className = 'connection-status disconnected';
            currentStatus.textContent = 'Ожидание подключения';
        }
    }
    
    // Показ уведомлений
    showNotification(message, type = 'info') {
        const notification = document.getElementById('notification');
        notification.textContent = message;
        notification.className = `notification ${type} show`;
        
        setTimeout(() => {
            notification.classList.remove('show');
        }, 3000);
    }
}

// Инициализация приложения после загрузки страницы
document.addEventListener('DOMContentLoaded', () => {
    window.comPortManager = new COMPortManager();
});