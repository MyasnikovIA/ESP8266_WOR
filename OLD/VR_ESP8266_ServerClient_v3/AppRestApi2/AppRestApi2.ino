#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <ArduinoJson.h>


bool CLE_EEPROM = false
;

// Настройки WiFi по умолчанию
const char* ap_ssid = "VR_HUB_ESP8266";
const char* ap_password = "12345678";

// Создание веб-сервера на порту 80
ESP8266WebServer server(80);

// DHCP сервер
#include <WiFiUdp.h>
WiFiUDP udp;
const unsigned int DHCP_SERVER_PORT = 67;
const unsigned int DHCP_CLIENT_PORT = 68;

// DHCP структуры
struct DHCPMessage {
    uint8_t op;      // Message op code / message type.
    uint8_t htype;   // Hardware address type
    uint8_t hlen;    // Hardware address length
    uint8_t hops;    // Hops
    uint32_t xid;    // Transaction ID
    uint16_t secs;   // Seconds
    uint16_t flags;  // Flags
    uint32_t ciaddr; // Client IP address
    uint32_t yiaddr; // Your (client) IP address
    uint32_t siaddr; // Next server IP address
    uint32_t giaddr; // Relay agent IP address
    uint8_t chaddr[16]; // Client hardware address
    uint8_t sname[64];  // Server host name
    uint8_t file[128];  // Boot file name
    uint8_t options[312]; // Optional parameters
};

// Структура для фиксации IP адреса
struct FixedIP {
  char mac[18];
  char ip[16];
};

// Структура для хранения информации об устройстве
struct DeviceInfo {
  char ip[16];
  char mac[18];
  char originalMac[18];  // Добавлено для отображения оригинального MAC
  char hostname[32];
  char customName[32];
  int rssi;
  bool ipFixed;
  bool connected;
  unsigned long lastSeen;
};

// Структура для настроек сети
struct NetworkSettings {
  char ssid[32];
  char password[32];
  char subnet[4];
  bool configured;
};

// Структура для хранения пользовательских имен устройств
struct DeviceAlias {
  char mac[18];
  char alias[32];
};

// Структура для комментариев устройств
struct DeviceComment {
  char mac[18];
  char comment[512];  // Многострочный текст комментария
};

// Структура для системных атрибутов
struct DeviceAttribute {
  char mac[18];
  char key[32];
  char value[64];
};

// Массив для хранения подключенных устройств
const int MAX_DEVICES = 20;
DeviceInfo devices[MAX_DEVICES];
int deviceCount = 0;

// Массив для хранения пользовательских имен
const int MAX_ALIASES = 30;
DeviceAlias deviceAliases[MAX_ALIASES];
int aliasCount = 0;

// Массив для комментариев устройств
const int MAX_COMMENTS = 30;
DeviceComment deviceComments[MAX_COMMENTS];
int commentCount = 0;

// Массив для системных атрибутов
const int MAX_ATTRIBUTES = 50;
DeviceAttribute deviceAttributes[MAX_ATTRIBUTES];
int attributeCount = 0;

// Массив для фиксированных IP адресов
const int MAX_FIXED_IPS = 20;
FixedIP fixedIPs[MAX_FIXED_IPS];
int fixedIPCount = 0;

// Настройки сети
NetworkSettings networkSettings;

// DHCP пул адресов
IPAddress dhcpStartIP;
IPAddress dhcpEndIP;
const int DHCP_POOL_SIZE = 50;
bool dhcpLeases[DHCP_POOL_SIZE];
String dhcpMacTable[DHCP_POOL_SIZE];

// Время последнего сканирования
unsigned long lastScanTime = 0;
const unsigned long SCAN_INTERVAL = 10000;

// Время последней записи в EEPROM
unsigned long lastEEPROMSave = 0;
const unsigned long EEPROM_SAVE_INTERVAL = 5000;
bool eepromDirty = false;

// Время последней проверки восстановления
unsigned long lastRecoveryCheck = 0;
const unsigned long RECOVERY_CHECK_INTERVAL = 30000;

// Флаги сбоев
bool wifiFailure = false;
bool memoryFailure = false;
unsigned long lastRestartAttempt = 0;
const unsigned long RESTART_INTERVAL = 60000;

// Безопасное копирование строк с проверкой границ
void safeStrcpy(char* dest, const char* src, size_t destSize) {
  if (dest == NULL || src == NULL || destSize == 0) return;
  
  size_t srcLen = strlen(src);
  if (srcLen >= destSize) {
    srcLen = destSize - 1;
  }
  
  strncpy(dest, src, srcLen);
  dest[srcLen] = '\0';
}

// Функция для поиска устройства в массиве
int findDeviceByMAC(const char* mac) {
  if (mac == NULL) return -1;
  
  for (int i = 0; i < deviceCount; i++) {
    if (strcmp(devices[i].mac, mac) == 0) {
      return i;
    }
  }
  return -1;
}

// Функция для поиска устройства по IP
int findDeviceByIP(const char* ip) {
  if (ip == NULL) return -1;
  
  for (int i = 0; i < deviceCount; i++) {
    if (strcmp(devices[i].ip, ip) == 0) {
      return i;
    }
  }
  return -1;
}

// Функция для поиска алиаса по MAC
int findAliasByMAC(const char* mac) {
  if (mac == NULL) return -1;
  
  for (int i = 0; i < aliasCount; i++) {
    if (strcmp(deviceAliases[i].mac, mac) == 0) {
      return i;
    }
  }
  return -1;
}

// Функция для поиска комментария по MAC
int findCommentByMAC(const char* mac) {
  if (mac == NULL) return -1;
  
  for (int i = 0; i < commentCount; i++) {
    if (strcmp(deviceComments[i].mac, mac) == 0) {
      return i;
    }
  }
  return -1;
}

// Функция для поиска атрибутов по MAC
int findAttributesByMAC(const char* mac, int* indices, int maxIndices) {
  if (mac == NULL) return 0;
  
  int count = 0;
  for (int i = 0; i < attributeCount && count < maxIndices; i++) {
    if (strcmp(deviceAttributes[i].mac, mac) == 0) {
      indices[count++] = i;
    }
  }
  return count;
}

// Функция для поиска фиксированного IP по MAC
int findFixedIPByMAC(const char* mac) {
  if (mac == NULL) return -1;
  
  for (int i = 0; i < fixedIPCount; i++) {
    if (strcmp(fixedIPs[i].mac, mac) == 0) {
      return i;
    }
  }
  return -1;
}

// Функция для получения отображаемого имени устройства
String getDisplayName(const char* mac, const char* originalHostname) {
  if (mac == NULL) return "Unknown";
  
  // Сначала ищем пользовательское имя
  int aliasIndex = findAliasByMAC(mac);
  if (aliasIndex != -1) {
    return String(deviceAliases[aliasIndex].alias);
  }
  
  // Если пользовательского имени нет, используем оригинальное
  return originalHostname && strlen(originalHostname) > 0 ? String(originalHostname) : "Unknown";
}

// Функция для получения комментария устройства
String getDeviceComment(const char* mac) {
  if (mac == NULL) return "";
  
  int commentIndex = findCommentByMAC(mac);
  if (commentIndex != -1) {
    return String(deviceComments[commentIndex].comment);
  }
  return "";
}

// Функция для добавления нового устройства с защитой от переполнения
bool addDevice(const char* ip, const char* mac, const char* hostname, int rssi) {
  if (ip == NULL || mac == NULL) return false;
  
  // Проверяем длину входных данных
  if (strlen(ip) >= 16 || strlen(mac) >= 18 || (hostname && strlen(hostname) >= 32)) {
    Serial.println("Error: Input data too long for device structure");
    return false;
  }
  
  int index = findDeviceByMAC(mac);
  
  if (index == -1) {
    if (deviceCount < MAX_DEVICES) {
      safeStrcpy(devices[deviceCount].ip, ip, sizeof(devices[deviceCount].ip));
      safeStrcpy(devices[deviceCount].mac, mac, sizeof(devices[deviceCount].mac));
      safeStrcpy(devices[deviceCount].originalMac, mac, sizeof(devices[deviceCount].originalMac));
      
      if (hostname && strlen(hostname) > 0) {
        safeStrcpy(devices[deviceCount].hostname, hostname, sizeof(devices[deviceCount].hostname));
      } else {
        safeStrcpy(devices[deviceCount].hostname, "Unknown", sizeof(devices[deviceCount].hostname));
      }
      
      safeStrcpy(devices[deviceCount].customName, "", sizeof(devices[deviceCount].customName));
      devices[deviceCount].rssi = rssi;
      
      // Проверяем есть ли фиксированный IP для этого MAC
      int fixedIndex = findFixedIPByMAC(mac);
      devices[deviceCount].ipFixed = (fixedIndex != -1);
      
      devices[deviceCount].connected = true;
      devices[deviceCount].lastSeen = millis();
      deviceCount++;
      
      Serial.printf("New device: %s (%s) - %s - RSSI: %d - IP Fixed: %s\n", 
                   hostname ? hostname : "Unknown", ip, mac, rssi, devices[deviceCount-1].ipFixed ? "YES" : "NO");
      return true;
    } else {
      Serial.println("Device limit reached!");
      return false;
    }
  } else {
    devices[index].rssi = rssi;
    safeStrcpy(devices[index].ip, ip, sizeof(devices[index].ip));
    devices[index].connected = true;
    devices[index].lastSeen = millis();
    if (hostname && strlen(hostname) > 0) {
      safeStrcpy(devices[index].hostname, hostname, sizeof(devices[index].hostname));
    }
    return true;
  }
}

// Функция сканирования сети
void scanNetwork() {
  Serial.println("Starting network scan...");
  
  // Помечаем все устройства как отключенные
  for (int i = 0; i < deviceCount; i++) {
    devices[i].connected = false;
  }
  
  // Проверяем активные подключения к точке доступа
  struct station_info *station = wifi_softap_get_station_info();
  while (station != NULL) {
    char mac[18];
    snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
             station->bssid[0], station->bssid[1], station->bssid[2],
             station->bssid[3], station->bssid[4], station->bssid[5]);
    
    char ip[16];
    IPAddress ipAddr = IPAddress(station->ip);
    snprintf(ip, sizeof(ip), "%d.%d.%d.%d", ipAddr[0], ipAddr[1], ipAddr[2], ipAddr[3]);
    
    // Добавляем устройство только если это не дубликат (по MAC-адресу)
    if (findDeviceByMAC(mac) == -1) {
      // Используем фиксированное значение RSSI, так как структура не содержит эту информацию
      addDevice(ip, mac, "WiFi Client", -50);
    } else {
      Serial.printf("Skipping duplicate device: %s (%s)\n", mac, ip);
    }
    
    station = STAILQ_NEXT(station, next);
  }
  wifi_softap_free_station_info();
  
  // Помечаем устройства как отключенные если не видели их больше минуты
  unsigned long currentTime = millis();
  for (int i = 0; i < deviceCount; i++) {
    if ((currentTime - devices[i].lastSeen) > 60000) {
      devices[i].connected = false;
    }
  }
  
  Serial.printf("Scan complete. Found %d devices\n", deviceCount);
  lastScanTime = millis();
}

// Инициализация DHCP сервера
void initDHCPServer() {
  int subnet = atoi(networkSettings.subnet);
  if (subnet < 1 || subnet > 254) {
    subnet = 50;
  }
  
  dhcpStartIP = IPAddress(192, 168, subnet, 2);
  dhcpEndIP = IPAddress(192, 168, subnet, 50);
  
  // Инициализация пула адресов
  for (int i = 0; i < DHCP_POOL_SIZE; i++) {
    dhcpLeases[i] = false;
    dhcpMacTable[i] = "";
  }
  
  // Запуск UDP сервера для DHCP
  if (udp.begin(DHCP_SERVER_PORT)) {
    Serial.println("DHCP Server started on port 67");
  } else {
    Serial.println("Failed to start DHCP server");
  }
}

// Обработка DHCP запросов
void handleDHCP() {
  int packetSize = udp.parsePacket();
  if (packetSize) {
    if (packetSize >= sizeof(DHCPMessage)) {
      Serial.println("DHCP packet too large");
      return;
    }
    
    DHCPMessage dhcpMsg;
    udp.read((uint8_t*)&dhcpMsg, sizeof(DHCPMessage));
    
    // Определяем тип DHCP сообщения
    uint8_t messageType = 0;
    for (int i = 0; i < 312; i++) {
      if (dhcpMsg.options[i] == 53) { // DHCP Message Type
        messageType = dhcpMsg.options[i + 2];
        break;
      }
    }
    
    if (messageType == 1) { // DHCP Discover
      handleDHCPDiscover(dhcpMsg);
    } else if (messageType == 3) { // DHCP Request
      handleDHCPRequest(dhcpMsg);
    }
  }
}

// Обработка DHCP Discover
void handleDHCPDiscover(DHCPMessage& discoverMsg) {
  Serial.println("DHCP Discover received");
  
  // Ищем свободный IP в пуле
  int freeIPIndex = -1;
  String clientMAC = macToString(discoverMsg.chaddr);
  
  // Сначала проверяем, есть ли уже аренда для этого MAC
  for (int i = 0; i < DHCP_POOL_SIZE; i++) {
    if (dhcpMacTable[i] == clientMAC) {
      freeIPIndex = i;
      break;
    }
  }
  
  // Если нет, ищем свободный IP
  if (freeIPIndex == -1) {
    for (int i = 0; i < DHCP_POOL_SIZE; i++) {
      if (!dhcpLeases[i]) {
        freeIPIndex = i;
        break;
      }
    }
  }
  
  if (freeIPIndex != -1) {
    // Отправляем DHCP Offer
    sendDHCPOffer(discoverMsg, freeIPIndex);
  }
}

// Обработка DHCP Request
void handleDHCPRequest(DHCPMessage& requestMsg) {
  Serial.println("DHCP Request received");
  
  String clientMAC = macToString(requestMsg.chaddr);
  int subnet = atoi(networkSettings.subnet);
  
  // Отправляем DHCP Ack
  sendDHCPAck(requestMsg, clientMAC);
  
  // Добавляем устройство в список
  IPAddress clientIP = IPAddress(192, 168, subnet, getIPFromMAC(clientMAC));
  addDevice(clientIP.toString().c_str(), clientMAC.c_str(), "DHCP Client", -50);
}

// Отправка DHCP Offer
void sendDHCPOffer(DHCPMessage& discoverMsg, int ipIndex) {
  DHCPMessage offerMsg;
  memset(&offerMsg, 0, sizeof(DHCPMessage));
  
  offerMsg.op = 2; // BOOTREPLY
  offerMsg.htype = 1; // Ethernet
  offerMsg.hlen = 6;
  offerMsg.xid = discoverMsg.xid;
  
  int subnet = atoi(networkSettings.subnet);
  offerMsg.yiaddr = IPAddress(192, 168, subnet, ipIndex + 2).v4();
  offerMsg.siaddr = WiFi.softAPIP().v4();
  
  memcpy(offerMsg.chaddr, discoverMsg.chaddr, 16);
  strcpy((char*)offerMsg.sname, "ESP8266_DHCP");
  
  // Добавляем опции
  int optIndex = 0;
  offerMsg.options[optIndex++] = 53; // DHCP Message Type
  offerMsg.options[optIndex++] = 1;
  offerMsg.options[optIndex++] = 2; // Offer
  
  offerMsg.options[optIndex++] = 1; // Subnet Mask
  offerMsg.options[optIndex++] = 4;
  uint32_t subnetMask = IPAddress(255, 255, 255, 0).v4();
  memcpy(&offerMsg.options[optIndex], &subnetMask, 4);
  optIndex += 4;
  
  offerMsg.options[optIndex++] = 3; // Router
  offerMsg.options[optIndex++] = 4;
  uint32_t router = WiFi.softAPIP().v4();
  memcpy(&offerMsg.options[optIndex], &router, 4);
  optIndex += 4;
  
  offerMsg.options[optIndex++] = 51; // IP Lease Time
  offerMsg.options[optIndex++] = 4;
  uint32_t leaseTime = 3600; // 1 hour
  memcpy(&offerMsg.options[optIndex], &leaseTime, 4);
  optIndex += 4;
  
  offerMsg.options[optIndex++] = 54; // DHCP Server Identifier
  offerMsg.options[optIndex++] = 4;
  memcpy(&offerMsg.options[optIndex], &router, 4);
  optIndex += 4;
  
  offerMsg.options[optIndex++] = 255; // End option
  
  // Отправка пакета
  udp.beginPacket(IPAddress(255, 255, 255, 255), DHCP_CLIENT_PORT);
  udp.write((uint8_t*)&offerMsg, sizeof(DHCPMessage));
  udp.endPacket();
  
  // Резервируем IP
  dhcpLeases[ipIndex] = true;
  dhcpMacTable[ipIndex] = macToString(discoverMsg.chaddr);
  
  Serial.printf("DHCP Offer sent for IP: 192.168.%d.%d\n", subnet, ipIndex + 2);
}

// Отправка DHCP Ack
void sendDHCPAck(DHCPMessage& requestMsg, const String& clientMAC) {
  DHCPMessage ackMsg;
  memset(&ackMsg, 0, sizeof(DHCPMessage));
  
  ackMsg.op = 2; // BOOTREPLY
  ackMsg.htype = 1;
  ackMsg.hlen = 6;
  ackMsg.xid = requestMsg.xid;
  
  int subnet = atoi(networkSettings.subnet);
  int clientIP = getIPFromMAC(clientMAC);
  ackMsg.yiaddr = IPAddress(192, 168, subnet, clientIP).v4();
  ackMsg.siaddr = WiFi.softAPIP().v4();
  
  memcpy(ackMsg.chaddr, requestMsg.chaddr, 16);
  strcpy((char*)ackMsg.sname, "ESP8266_DHCP");
  
  // Добавляем опции
  int optIndex = 0;
  ackMsg.options[optIndex++] = 53; // DHCP Message Type
  ackMsg.options[optIndex++] = 1;
  ackMsg.options[optIndex++] = 5; // ACK
  
  ackMsg.options[optIndex++] = 1; // Subnet Mask
  ackMsg.options[optIndex++] = 4;
  uint32_t subnetMask = IPAddress(255, 255, 255, 0).v4();
  memcpy(&ackMsg.options[optIndex], &subnetMask, 4);
  optIndex += 4;
  
  ackMsg.options[optIndex++] = 3; // Router
  ackMsg.options[optIndex++] = 4;
  uint32_t router = WiFi.softAPIP().v4();
  memcpy(&ackMsg.options[optIndex], &router, 4);
  optIndex += 4;
  
  ackMsg.options[optIndex++] = 51; // IP Lease Time
  ackMsg.options[optIndex++] = 4;
  uint32_t leaseTime = 3600;
  memcpy(&ackMsg.options[optIndex], &leaseTime, 4);
  optIndex += 4;
  
  ackMsg.options[optIndex++] = 54; // DHCP Server Identifier
  ackMsg.options[optIndex++] = 4;
  memcpy(&ackMsg.options[optIndex], &router, 4);
  optIndex += 4;
  
  ackMsg.options[optIndex++] = 255; // End option
  
  udp.beginPacket(IPAddress(255, 255, 255, 255), DHCP_CLIENT_PORT);
  udp.write((uint8_t*)&ackMsg, sizeof(DHCPMessage));
  udp.endPacket();
  
  Serial.printf("DHCP ACK sent for IP: 192.168.%d.%d to MAC: %s\n", subnet, clientIP, clientMAC.c_str());
}

// Вспомогательная функция для преобразования MAC в строку
String macToString(uint8_t* mac) {
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(macStr);
}

// Получение IP из MAC
int getIPFromMAC(const String& mac) {
  for (int i = 0; i < DHCP_POOL_SIZE; i++) {
    if (dhcpMacTable[i] == mac) {
      return i + 2;
    }
  }
  return -1;
}

// Функция для сохранения настроек сети в EEPROM
void saveNetworkSettingsToEEPROM() {
  EEPROM.begin(4096);
  
  int addr = 0;
  EEPROM.write(addr++, networkSettings.configured ? 1 : 0);
  
  // Сохраняем SSID
  size_t ssidLen = strlen(networkSettings.ssid);
  if (ssidLen > 31) ssidLen = 31;
  EEPROM.write(addr++, ssidLen);
  for (size_t j = 0; j < ssidLen; j++) {
    EEPROM.write(addr++, networkSettings.ssid[j]);
  }
  
  // Сохраняем пароль
  size_t passwordLen = strlen(networkSettings.password);
  if (passwordLen > 31) passwordLen = 31;
  EEPROM.write(addr++, passwordLen);
  for (size_t j = 0; j < passwordLen; j++) {
    EEPROM.write(addr++, networkSettings.password[j]);
  }
  
  // Сохраняем подсеть
  size_t subnetLen = strlen(networkSettings.subnet);
  if (subnetLen > 3) subnetLen = 3;
  EEPROM.write(addr++, subnetLen);
  for (size_t j = 0; j < subnetLen; j++) {
    EEPROM.write(addr++, networkSettings.subnet[j]);
  }
  
  if (EEPROM.commit()) {
    Serial.println("Network settings saved to EEPROM");
  } else {
    Serial.println("Error saving network settings to EEPROM");
    memoryFailure = true;
  }
  EEPROM.end();
}

// Функция для загрузки настроек сети из EEPROM
void loadNetworkSettingsFromEEPROM() {
  EEPROM.begin(4096);
  
  int addr = 0;
  networkSettings.configured = (EEPROM.read(addr++) == 1);
  
  if (networkSettings.configured) {
    // Загружаем SSID
    int ssidLen = EEPROM.read(addr++);
    if (ssidLen > 31) ssidLen = 31;
    for (int j = 0; j < ssidLen; j++) {
      networkSettings.ssid[j] = EEPROM.read(addr++);
    }
    networkSettings.ssid[ssidLen] = '\0';
    
    // Загружаем пароль
    int passwordLen = EEPROM.read(addr++);
    if (passwordLen > 31) passwordLen = 31;
    for (int j = 0; j < passwordLen; j++) {
      networkSettings.password[j] = EEPROM.read(addr++);
    }
    networkSettings.password[passwordLen] = '\0';
    
    // Загружаем подсеть
    int subnetLen = EEPROM.read(addr++);
    if (subnetLen > 3) subnetLen = 3;
    for (int j = 0; j < subnetLen; j++) {
      networkSettings.subnet[j] = EEPROM.read(addr++);
    }
    networkSettings.subnet[subnetLen] = '\0';
  } else {
    // Значения по умолчанию
    safeStrcpy(networkSettings.ssid, ap_ssid, sizeof(networkSettings.ssid));
    safeStrcpy(networkSettings.password, ap_password, sizeof(networkSettings.password));
    safeStrcpy(networkSettings.subnet, "50", sizeof(networkSettings.subnet));
  }
  
  EEPROM.end();
  
  Serial.printf("Network settings loaded: SSID=%s, Subnet=%s\n", 
                networkSettings.ssid, networkSettings.subnet);
}

// Функция для сохранения алиасов устройств, комментариев и атрибутов в EEPROM
void saveDeviceDataToEEPROM() {
  EEPROM.begin(4096);
  
  int addr = 512;
  EEPROM.write(addr++, aliasCount > MAX_ALIASES ? MAX_ALIASES : aliasCount);
  
  for (int i = 0; i < aliasCount && i < MAX_ALIASES; i++) {
    // Сохраняем MAC
    size_t macLen = strlen(deviceAliases[i].mac);
    if (macLen > 17) macLen = 17;
    EEPROM.write(addr++, macLen);
    for (size_t j = 0; j < macLen; j++) {
      EEPROM.write(addr++, deviceAliases[i].mac[j]);
    }
    
    // Сохраняем алиас
    size_t aliasLen = strlen(deviceAliases[i].alias);
    if (aliasLen > 31) aliasLen = 31;
    EEPROM.write(addr++, aliasLen);
    for (size_t j = 0; j < aliasLen; j++) {
      EEPROM.write(addr++, deviceAliases[i].alias[j]);
    }
  }
  
  // Сохраняем комментарии
  addr = 1024;
  EEPROM.write(addr++, commentCount > MAX_COMMENTS ? MAX_COMMENTS : commentCount);
  
  for (int i = 0; i < commentCount && i < MAX_COMMENTS; i++) {
    // Сохраняем MAC
    size_t macLen = strlen(deviceComments[i].mac);
    if (macLen > 17) macLen = 17;
    EEPROM.write(addr++, macLen);
    for (size_t j = 0; j < macLen; j++) {
      EEPROM.write(addr++, deviceComments[i].mac[j]);
    }
    
    // Сохраняем комментарий
    size_t commentLen = strlen(deviceComments[i].comment);
    if (commentLen > 511) commentLen = 511;
    EEPROM.write(addr++, (commentLen >> 8) & 0xFF); // Старший байт длины
    EEPROM.write(addr++, commentLen & 0xFF);        // Младший байт длины
    for (size_t j = 0; j < commentLen; j++) {
      EEPROM.write(addr++, deviceComments[i].comment[j]);
    }
  }
  
  // Сохраняем атрибуты
  addr = 2048;
  EEPROM.write(addr++, attributeCount > MAX_ATTRIBUTES ? MAX_ATTRIBUTES : attributeCount);
  
  for (int i = 0; i < attributeCount && i < MAX_ATTRIBUTES; i++) {
    // Сохраняем MAC
    size_t macLen = strlen(deviceAttributes[i].mac);
    if (macLen > 17) macLen = 17;
    EEPROM.write(addr++, macLen);
    for (size_t j = 0; j < macLen; j++) {
      EEPROM.write(addr++, deviceAttributes[i].mac[j]);
    }
    
    // Сохраняем ключ
    size_t keyLen = strlen(deviceAttributes[i].key);
    if (keyLen > 31) keyLen = 31;
    EEPROM.write(addr++, keyLen);
    for (size_t j = 0; j < keyLen; j++) {
      EEPROM.write(addr++, deviceAttributes[i].key[j]);
    }
    
    // Сохраняем значение
    size_t valueLen = strlen(deviceAttributes[i].value);
    if (valueLen > 63) valueLen = 63;
    EEPROM.write(addr++, valueLen);
    for (size_t j = 0; j < valueLen; j++) {
      EEPROM.write(addr++, deviceAttributes[i].value[j]);
    }
  }
  
  // Сохраняем фиксированные IP
  addr = 3072;
  EEPROM.write(addr++, fixedIPCount > MAX_FIXED_IPS ? MAX_FIXED_IPS : fixedIPCount);
  
  for (int i = 0; i < fixedIPCount && i < MAX_FIXED_IPS; i++) {
    // Сохраняем MAC
    size_t macLen = strlen(fixedIPs[i].mac);
    if (macLen > 17) macLen = 17;
    EEPROM.write(addr++, macLen);
    for (size_t j = 0; j < macLen; j++) {
      EEPROM.write(addr++, fixedIPs[i].mac[j]);
    }
    
    // Сохраняем IP
    size_t ipLen = strlen(fixedIPs[i].ip);
    if (ipLen > 15) ipLen = 15;
    EEPROM.write(addr++, ipLen);
    for (size_t j = 0; j < ipLen; j++) {
      EEPROM.write(addr++, fixedIPs[i].ip[j]);
    }
  }
  
  if (EEPROM.commit()) {
    Serial.println("Device data saved to EEPROM");
    eepromDirty = false;
  } else {
    Serial.println("Error saving device data to EEPROM");
    memoryFailure = true;
  }
  EEPROM.end();
}

// Функция для загрузки алиасов устройств, комментариев и атрибутов из EEPROM
void loadDeviceDataFromEEPROM() {
  EEPROM.begin(4096);
  
  int addr = 512;
  aliasCount = EEPROM.read(addr++);
  
  if (aliasCount > MAX_ALIASES) {
    aliasCount = 0;
  }
  
  for (int i = 0; i < aliasCount; i++) {
    // Загружаем MAC
    int macLen = EEPROM.read(addr++);
    if (macLen > 17) macLen = 17;
    for (int j = 0; j < macLen; j++) {
      deviceAliases[i].mac[j] = EEPROM.read(addr++);
    }
    deviceAliases[i].mac[macLen] = '\0';
    
    // Загружаем алиас
    int aliasLen = EEPROM.read(addr++);
    if (aliasLen > 31) aliasLen = 31;
    for (int j = 0; j < aliasLen; j++) {
      deviceAliases[i].alias[j] = EEPROM.read(addr++);
    }
    deviceAliases[i].alias[aliasLen] = '\0';
  }
  
  // Загружаем комментарии
  addr = 1024;
  commentCount = EEPROM.read(addr++);
  
  if (commentCount > MAX_COMMENTS) {
    commentCount = 0;
  }
  
  for (int i = 0; i < commentCount; i++) {
    // Загружаем MAC
    int macLen = EEPROM.read(addr++);
    if (macLen > 17) macLen = 17;
    for (int j = 0; j < macLen; j++) {
      deviceComments[i].mac[j] = EEPROM.read(addr++);
    }
    deviceComments[i].mac[macLen] = '\0';
    
    // Загружаем комментарий
    int commentLen = (EEPROM.read(addr++) << 8) | EEPROM.read(addr++);
    if (commentLen > 511) commentLen = 511;
    for (int j = 0; j < commentLen; j++) {
      deviceComments[i].comment[j] = EEPROM.read(addr++);
    }
    deviceComments[i].comment[commentLen] = '\0';
  }
  
  // Загружаем атрибуты
  addr = 2048;
  attributeCount = EEPROM.read(addr++);
  
  if (attributeCount > MAX_ATTRIBUTES) {
    attributeCount = 0;
  }
  
  for (int i = 0; i < attributeCount; i++) {
    // Загружаем MAC
    int macLen = EEPROM.read(addr++);
    if (macLen > 17) macLen = 17;
    for (int j = 0; j < macLen; j++) {
      deviceAttributes[i].mac[j] = EEPROM.read(addr++);
    }
    deviceAttributes[i].mac[macLen] = '\0';
    
    // Загружаем ключ
    int keyLen = EEPROM.read(addr++);
    if (keyLen > 31) keyLen = 31;
    for (int j = 0; j < keyLen; j++) {
      deviceAttributes[i].key[j] = EEPROM.read(addr++);
    }
    deviceAttributes[i].key[keyLen] = '\0';
    
    // Загружаем значение
    int valueLen = EEPROM.read(addr++);
    if (valueLen > 63) valueLen = 63;
    for (int j = 0; j < valueLen; j++) {
      deviceAttributes[i].value[j] = EEPROM.read(addr++);
    }
    deviceAttributes[i].value[valueLen] = '\0';
  }
  
  // Загружаем фиксированные IP
  addr = 3072;
  fixedIPCount = EEPROM.read(addr++);
  
  if (fixedIPCount > MAX_FIXED_IPS) {
    fixedIPCount = 0;
  }
  
  for (int i = 0; i < fixedIPCount; i++) {
    // Загружаем MAC
    int macLen = EEPROM.read(addr++);
    if (macLen > 17) macLen = 17;
    for (int j = 0; j < macLen; j++) {
      fixedIPs[i].mac[j] = EEPROM.read(addr++);
    }
    fixedIPs[i].mac[macLen] = '\0';
    
    // Загружаем IP
    int ipLen = EEPROM.read(addr++);
    if (ipLen > 15) ipLen = 15;
    for (int j = 0; j < ipLen; j++) {
      fixedIPs[i].ip[j] = EEPROM.read(addr++);
    }
    fixedIPs[i].ip[ipLen] = '\0';
  }
  
  EEPROM.end();
  
  Serial.printf("Loaded %d aliases, %d comments, %d attributes and %d fixed IPs from EEPROM\n", 
                aliasCount, commentCount, attributeCount, fixedIPCount);
}

// Функция восстановления при сбоях
void recoveryCheck() {
  unsigned long currentTime = millis();
  
  // Проверяем сбои WiFi
  if (wifiFailure) {
    Serial.println("WiFi failure detected, attempting recovery...");
    
    // Перезапускаем точку доступа
    WiFi.softAPdisconnect(true);
    delay(1000);
    
    int subnet = atoi(networkSettings.subnet);
    IPAddress local_ip(192, 168, subnet, 1);
    IPAddress gateway(192, 168, subnet, 1);
    IPAddress subnet_mask(255, 255, 255, 0);
    
    WiFi.softAPConfig(local_ip, gateway, subnet_mask);
    
    if (WiFi.softAP(networkSettings.ssid, networkSettings.password)) {
      Serial.println("WiFi AP restarted successfully");
      wifiFailure = false;
    } else {
      Serial.println("Failed to restart WiFi AP");
    }
  }
  
  // Проверяем сбои памяти
  if (memoryFailure) {
    Serial.println("Memory failure detected, attempting recovery...");
    
    // Перезапускаем EEPROM
    EEPROM.begin(4096);
    EEPROM.end();
    
    // Перезагружаем настройки
    loadNetworkSettingsFromEEPROM();
    loadDeviceDataFromEEPROM();
    
    memoryFailure = false;
    Serial.println("Memory recovery completed");
  }
  
  // Проверяем необходимость перезагрузки
  if (currentTime - lastRestartAttempt > RESTART_INTERVAL) {
    if (WiFi.status() != WL_CONNECTED && WiFi.softAPgetStationNum() == 0) {
      Serial.println("System appears unstable, attempting restart...");
      ESP.restart();
    }
    lastRestartAttempt = currentTime;
  }
  
  lastRecoveryCheck = currentTime;
}

// Обработчик OPTIONS запросов для CORS
void handleOptions() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  server.send(200, "text/plain", "");
}

// Функция для отправки ответа с CORS заголовками
void sendCORSResponse(int code, const String& content_type, const String& content) {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  server.send(code, content_type, content);
}

// API для получения списка устройств
void handleApiDevices() {
  if (server.method() == HTTP_GET) {
    DynamicJsonDocument doc(8192);
    JsonArray devicesArray = doc.createNestedArray("devices");
    
    // Подсчитываем статистику
    int onlineCount = 0;
    int fixedCount = 0;
    
    for (int i = 0; i < deviceCount; i++) {
      if (devices[i].connected) onlineCount++;
      if (devices[i].ipFixed) fixedCount++;
    }
    
    // Добавляем все устройства
    for (int i = 0; i < deviceCount; i++) {
      JsonObject deviceObj = devicesArray.createNestedObject();
      
      deviceObj["ip"] = devices[i].ip;
      deviceObj["mac"] = devices[i].mac;
      deviceObj["originalMac"] = devices[i].originalMac;
      deviceObj["hostname"] = devices[i].hostname;
      deviceObj["displayName"] = getDisplayName(devices[i].mac, devices[i].hostname);
      deviceObj["comment"] = getDeviceComment(devices[i].mac);
      deviceObj["rssi"] = devices[i].rssi;
      deviceObj["ipFixed"] = devices[i].ipFixed;
      deviceObj["connected"] = devices[i].connected;
      deviceObj["lastSeen"] = devices[i].lastSeen;
      
      // Добавляем атрибуты
      JsonObject attributesObj = deviceObj.createNestedObject("attributes");
      int attributeIndices[MAX_ATTRIBUTES];
      int attrCount = findAttributesByMAC(devices[i].mac, attributeIndices, MAX_ATTRIBUTES);
      for (int j = 0; j < attrCount; j++) {
        attributesObj[deviceAttributes[attributeIndices[j]].key] = deviceAttributes[attributeIndices[j]].value;
      }
    }
    
    doc["type"] = "devices_list";
    doc["totalDevices"] = deviceCount;
    doc["onlineDevices"] = onlineCount;
    doc["fixedIPs"] = fixedCount;
    doc["espIp"] = WiFi.softAPIP().toString();
    
    String json;
    if (serializeJson(doc, json) == 0) {
      sendCORSResponse(500, "application/json", "{\"status\":\"error\",\"message\":\"Failed to serialize JSON\"}");
    } else {
      sendCORSResponse(200, "application/json", json);
    }
  } else if (server.method() == HTTP_OPTIONS) {
    handleOptions();
  } else {
    sendCORSResponse(405, "application/json", "{\"status\":\"error\",\"message\":\"Method not allowed\"}");
  }
}

// API для запуска сканирования сети
void handleApiScan() {
  if (server.method() == HTTP_POST) {
    scanNetwork();
    sendCORSResponse(200, "application/json", "{\"status\":\"success\",\"message\":\"Network scan completed\"}");
  } else if (server.method() == HTTP_OPTIONS) {
    handleOptions();
  } else {
    sendCORSResponse(405, "application/json", "{\"status\":\"error\",\"message\":\"Method not allowed\"}");
  }
}

// API для переименования устройства с CORS (расширенный для комментариев и атрибутов)
void handleApiRename() {
  if (server.method() == HTTP_POST) {
    String mac = server.arg("mac");
    String name = server.arg("name");
    String comment = server.arg("comment");
    String attributes = server.arg("attributes");
    
    Serial.printf("Rename request - MAC: %s, Name: %s\n", mac.c_str(), name.c_str());
    
    if (mac.length() > 0 && name.length() > 0 && name.length() <= 30) {
      // Ищем существующий алиас
      int aliasIndex = findAliasByMAC(mac.c_str());
      
      if (aliasIndex != -1) {
        // Обновляем существующий алиас
        safeStrcpy(deviceAliases[aliasIndex].alias, name.c_str(), sizeof(deviceAliases[aliasIndex].alias));
        Serial.printf("Updated alias for MAC %s: %s\n", mac.c_str(), name.c_str());
      } else {
        // Создаем новый алиас
        if (aliasCount < MAX_ALIASES) {
          safeStrcpy(deviceAliases[aliasCount].mac, mac.c_str(), sizeof(deviceAliases[aliasCount].mac));
          safeStrcpy(deviceAliases[aliasCount].alias, name.c_str(), sizeof(deviceAliases[aliasCount].alias));
          aliasCount++;
          Serial.printf("Created new alias for MAC %s: %s\n", mac.c_str(), name.c_str());
        } else {
          sendCORSResponse(507, "application/json", "{\"status\":\"error\",\"message\":\"Alias limit reached\"}");
          return;
        }
      }
      
      // Обработка комментария
      if (comment.length() > 0) {
        int commentIndex = findCommentByMAC(mac.c_str());
        
        if (commentIndex != -1) {
          // Обновляем существующий комментарий
          safeStrcpy(deviceComments[commentIndex].comment, comment.c_str(), sizeof(deviceComments[commentIndex].comment));
          Serial.printf("Updated comment for MAC %s\n", mac.c_str());
        } else {
          // Создаем новый комментарий
          if (commentCount < MAX_COMMENTS) {
            safeStrcpy(deviceComments[commentCount].mac, mac.c_str(), sizeof(deviceComments[commentCount].mac));
            safeStrcpy(deviceComments[commentCount].comment, comment.c_str(), sizeof(deviceComments[commentCount].comment));
            commentCount++;
            Serial.printf("Created new comment for MAC %s\n", mac.c_str());
          } else {
            Serial.println("Comment limit reached!");
          }
        }
      } else {
        // Если комментарий пустой, удаляем его
        int commentIndex = findCommentByMAC(mac.c_str());
        if (commentIndex != -1) {
          // Удаляем комментарий (сдвигаем массив)
          for (int i = commentIndex; i < commentCount - 1; i++) {
            memcpy(&deviceComments[i], &deviceComments[i + 1], sizeof(DeviceComment));
          }
          commentCount--;
          Serial.printf("Removed comment for MAC %s\n", mac.c_str());
        }
      }
      
      // Обработка атрибутов
      if (attributes.length() > 0) {
        // Сначала удаляем все существующие атрибуты для этого MAC
        int attrIndices[MAX_ATTRIBUTES];
        int attrCount = findAttributesByMAC(mac.c_str(), attrIndices, MAX_ATTRIBUTES);
        for (int i = attrCount - 1; i >= 0; i--) {
          // Удаляем атрибут (сдвигаем массив)
          for (int j = attrIndices[i]; j < attributeCount - 1; j++) {
            memcpy(&deviceAttributes[j], &deviceAttributes[j + 1], sizeof(DeviceAttribute));
          }
          attributeCount--;
        }
        
        // Парсим и добавляем новые атрибуты
        int startPos = 0;
        while (startPos < attributes.length() && attributeCount < MAX_ATTRIBUTES) {
          int endPos = attributes.indexOf('\n', startPos);
          if (endPos == -1) endPos = attributes.length();
          
          String attributeLine = attributes.substring(startPos, endPos);
          int colonPos = attributeLine.indexOf(':');
          
          if (colonPos != -1) {
            String key = attributeLine.substring(0, colonPos);
            String value = attributeLine.substring(colonPos + 1);
            
            key.trim();
            value.trim();
            
            // Удаляем кавычки если есть
            if (value.startsWith("\"") && value.endsWith("\"")) {
              value = value.substring(1, value.length() - 1);
            }
            
            if (key.length() > 0 && key.length() <= 31 && value.length() <= 63) {
              safeStrcpy(deviceAttributes[attributeCount].mac, mac.c_str(), sizeof(deviceAttributes[attributeCount].mac));
              safeStrcpy(deviceAttributes[attributeCount].key, key.c_str(), sizeof(deviceAttributes[attributeCount].key));
              safeStrcpy(deviceAttributes[attributeCount].value, value.c_str(), sizeof(deviceAttributes[attributeCount].value));
              attributeCount++;
              
              Serial.printf("Added attribute for MAC %s: %s=%s\n", mac.c_str(), key.c_str(), value.c_str());
            }
          }
          
          startPos = endPos + 1;
        }
      } else {
        // Если атрибуты пустые, удаляем все атрибуты для этого MAC
        int attrIndices[MAX_ATTRIBUTES];
        int attrCount = findAttributesByMAC(mac.c_str(), attrIndices, MAX_ATTRIBUTES);
        for (int i = attrCount - 1; i >= 0; i--) {
          // Удаляем атрибут (сдвигаем массив)
          for (int j = attrIndices[i]; j < attributeCount - 1; j++) {
            memcpy(&deviceAttributes[j], &deviceAttributes[j + 1], sizeof(DeviceAttribute));
          }
          attributeCount--;
        }
        Serial.printf("Removed all attributes for MAC %s\n", mac.c_str());
      }
      
      // Помечаем EEPROM как требующее сохранения
      eepromDirty = true;
      lastEEPROMSave = millis();
      
      sendCORSResponse(200, "application/json", "{\"status\":\"success\",\"message\":\"Device settings saved\"}");
      
    } else {
      sendCORSResponse(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid parameters\"}");
    }
  } else if (server.method() == HTTP_OPTIONS) {
    handleOptions();
  } else {
    sendCORSResponse(405, "application/json", "{\"status\":\"error\",\"message\":\"Method not allowed\"}");
  }
}

// API для фиксации IP адреса с CORS
void handleApiFixIP() {
  if (server.method() == HTTP_POST) {
    String mac = server.arg("mac");
    String ip = server.arg("ip");
    
    Serial.printf("Fix IP request - MAC: %s, IP: %s\n", mac.c_str(), ip.c_str());
    
    if (mac.length() > 0 && ip.length() > 0) {
      int ipNum = ip.toInt();
      if (ipNum < 2 || ipNum > 254) {
        sendCORSResponse(400, "application/json", "{\"status\":\"error\",\"message\":\"IP must be between 2 and 254\"}");
        return;
      }
      
      // Формируем полный IP адрес
      int subnet = atoi(networkSettings.subnet);
      char fullIP[16];
      snprintf(fullIP, sizeof(fullIP), "192.168.%d.%s", subnet, ip.c_str());
      
      // Ищем существующую запись
      int fixedIndex = findFixedIPByMAC(mac.c_str());
      
      if (fixedIndex != -1) {
        // Обновляем существующую запись
        safeStrcpy(fixedIPs[fixedIndex].ip, fullIP, sizeof(fixedIPs[fixedIndex].ip));
        Serial.printf("Updated fixed IP for MAC %s: %s\n", mac.c_str(), fullIP);
      } else {
        // Создаем новую запись
        if (fixedIPCount < MAX_FIXED_IPS) {
          safeStrcpy(fixedIPs[fixedIPCount].mac, mac.c_str(), sizeof(fixedIPs[fixedIPCount].mac));
          safeStrcpy(fixedIPs[fixedIPCount].ip, fullIP, sizeof(fixedIPs[fixedIPCount].ip));
          fixedIPCount++;
          Serial.printf("Created new fixed IP for MAC %s: %s\n", mac.c_str(), fullIP);
        } else {
          sendCORSResponse(507, "application/json", "{\"status\":\"error\",\"message\":\"Fixed IP limit reached\"}");
          return;
        }
      }
      
      // Обновляем статус фиксации в устройствах
      int deviceIndex = findDeviceByMAC(mac.c_str());
      if (deviceIndex != -1) {
        devices[deviceIndex].ipFixed = true;
      }
      
      // Помечаем EEPROM как требующее сохранения
      eepromDirty = true;
      lastEEPROMSave = millis();
      
      sendCORSResponse(200, "application/json", "{\"status\":\"success\",\"message\":\"IP address fixed. Device will get this IP on next connection.\"}");
      
    } else {
      sendCORSResponse(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid parameters\"}");
    }
  } else if (server.method() == HTTP_OPTIONS) {
    handleOptions();
  } else {
    sendCORSResponse(405, "application/json", "{\"status\":\"error\",\"message\":\"Method not allowed\"}");
  }
}

// API для снятия фиксации IP адреса с CORS
void handleApiUnfixIP() {
  if (server.method() == HTTP_POST) {
    String mac = server.arg("mac");
    
    Serial.printf("Unfix IP request - MAC: %s\n", mac.c_str());
    
    if (mac.length() > 0) {
      // Ищем запись
      int fixedIndex = findFixedIPByMAC(mac.c_str());
      
      if (fixedIndex != -1) {
        // Удаляем запись (сдвигаем массив)
        for (int i = fixedIndex; i < fixedIPCount - 1; i++) {
          memcpy(&fixedIPs[i], &fixedIPs[i + 1], sizeof(FixedIP));
        }
        fixedIPCount--;
        Serial.printf("Removed fixed IP for MAC %s\n", mac.c_str());
        
        // Обновляем статус фиксации в устройствах
        int deviceIndex = findDeviceByMAC(mac.c_str());
        if (deviceIndex != -1) {
          devices[deviceIndex].ipFixed = false;
        }
        
        // Помечаем EEPROM как требующее сохранения
        eepromDirty = true;
        lastEEPROMSave = millis();
        
        sendCORSResponse(200, "application/json", "{\"status\":\"success\",\"message\":\"IP address unfixed\"}");
      } else {
        sendCORSResponse(404, "application/json", "{\"status\":\"error\",\"message\":\"Fixed IP not found\"}");
      }
    } else {
      sendCORSResponse(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid parameters\"}");
    }
  } else if (server.method() == HTTP_OPTIONS) {
    handleOptions();
  } else {
    sendCORSResponse(405, "application/json", "{\"status\":\"error\",\"message\":\"Method not allowed\"}");
  }
}

// API для сохранения настроек с CORS
void handleApiSettings() {
  if (server.method() == HTTP_POST) {
    String ssid = server.arg("ssid");
    String password = server.arg("password");
    String subnet = server.arg("subnet");
    
    Serial.printf("Settings request - SSID: %s, Subnet: %s\n", ssid.c_str(), subnet.c_str());
    
    if (ssid.length() > 0 && password.length() >= 8 && subnet.length() > 0) {
      // Сохраняем новые настройки
      safeStrcpy(networkSettings.ssid, ssid.c_str(), sizeof(networkSettings.ssid));
      safeStrcpy(networkSettings.password, password.c_str(), sizeof(networkSettings.password));
      safeStrcpy(networkSettings.subnet, subnet.c_str(), sizeof(networkSettings.subnet));
      networkSettings.configured = true;
      
      saveNetworkSettingsToEEPROM();
      
      sendCORSResponse(200, "application/json", "{\"status\":\"success\"}");
      Serial.println("Settings saved successfully");
      
      // Перезагрузка для применения новых настроек
      delay(1000);
      ESP.restart();
      
    } else {
      sendCORSResponse(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid parameters - check SSID, password (min 8 chars) and subnet\"}");
      Serial.println("Error: Invalid parameters");
    }
  } else if (server.method() == HTTP_OPTIONS) {
    handleOptions();
  } else {
    sendCORSResponse(405, "application/json", "{\"status\":\"error\",\"message\":\"Method not allowed\"}");
    Serial.println("Error: Method not allowed");
  }
}

// API для получения текущих настроек
void handleApiGetSettings() {
  if (server.method() == HTTP_GET) {
    DynamicJsonDocument doc(512);
    
    doc["ssid"] = networkSettings.ssid;
    doc["subnet"] = networkSettings.subnet;
    doc["configured"] = networkSettings.configured;
    
    String json;
    if (serializeJson(doc, json) == 0) {
      sendCORSResponse(500, "application/json", "{\"status\":\"error\",\"message\":\"Failed to serialize JSON\"}");
    } else {
      sendCORSResponse(200, "application/json", json);
    }
  } else if (server.method() == HTTP_OPTIONS) {
    handleOptions();
  } else {
    sendCORSResponse(405, "application/json", "{\"status\":\"error\",\"message\":\"Method not allowed\"}");
  }
}

// ВЕБ-ИНТЕРФЕЙС
const char htmlPage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>ESP8266 Network Monitor</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; background: #f5f5f5; color: #333; }
        .container { max-width: 1200px; margin: 0 auto; background: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }
        .header { text-align: center; margin-bottom: 20px; padding: 15px; background: #2c3e50; color: white; border-radius: 6px; }
        .stats { display: flex; justify-content: space-around; margin-bottom: 20px; flex-wrap: wrap; }
        .stat-card { background: #ecf0f1; padding: 15px; border-radius: 6px; text-align: center; min-width: 120px; margin: 5px; }
        .stat-number { font-size: 24px; font-weight: bold; color: #2c3e50; }
        .stat-label { color: #7f8c8d; font-size: 12px; }
        table { width: 100%; border-collapse: collapse; margin-top: 10px; }
        th, td { padding: 12px; text-align: left; border-bottom: 1px solid #ddd; }
        th { background-color: #e9ecef; }
        button { background: #4CAF50; color: white; padding: 10px 16px; border: none; border-radius: 4px; cursor: pointer; margin: 2px; }
        .btn-danger { background: #dc3545; }
        .btn-warning { background: #ffc107; color: #212529; }
        .tablinks { color: rgb(0,0,0);}
        .modal { display: none; position: fixed; z-index: 1000; left: 0; top: 0; width: 100%; height: 100%; background-color: rgba(0,0,0,0.5); }
        .modal-content { background-color: #fefefe; margin: 5% auto; padding: 20px; border: 1px solid #888; width: 90%; max-width: 600px; border-radius: 8px; }
        input[type='text'], input[type='password'], input[type='number'], textarea { width: 100%; padding: 8px; margin: 5px 0; border: 1px solid #ccc; border-radius: 4px; box-sizing: border-box; }
        textarea { height: 100px; resize: vertical; }
        .info { background: #e7f3fe; padding: 15px; border-radius: 6px; margin: 15px 0; border-left: 4px solid #2196F3; }
        .warning { background: #fff3cd; padding: 15px; border-radius: 6px; margin: 15px 0; border-left: 4px solid #ffc107; }
        .connected { color: #28a745; font-weight: bold; }
        .disconnected { color: #dc3545; font-weight: bold; }
        .tab { overflow: hidden; border: 1px solid #ccc; background-color: #f8f9fa; margin-bottom: 20px; }
        .tab button { background-color: inherit; float: left; border: none; outline: none; cursor: pointer; padding: 12px 20px; transition: 0.3s; }
        .tab button:hover { background-color: #e9ecef; }
        .tab button.active { background-color: #4CAF50; color: white; }
        .tabcontent { display: none; padding: 20px; border: 1px solid #ccc; border-top: none; }
        .checkbox-container { margin: 10px 0; display: flex; align-items: center; }
        .checkbox-container input[type='checkbox'] { margin-right: 10px; }
        .ip-input { display: none; margin-top: 10px; }
        h1, h2, h3 { margin-bottom: 15px; }
        .device-actions { display: flex; gap: 5px; }
        .refresh-info { text-align: center; color: #6c757d; font-size: 12px; margin-top: 10px; }
        .attribute-row { display: flex; margin-bottom: 5px; gap: 5px; }
        .attribute-key { flex: 2; }
        .attribute-value { flex: 3; }
        .attribute-remove { flex-shrink: 0; background: #dc3545 !important; }
        .section { margin-bottom: 20px; padding: 15px; border: 1px solid #ddd; border-radius: 4px; }
        .section-title { font-weight: bold; margin-bottom: 10px; color: #2c3e50; }
        .attributes-container { margin-top: 10px; }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>ESP8266 Network Monitor</h1>
            <p>Real-time network device monitoring and management</p>
        </div>
        
        <div class="info">
            <strong>Current IP:</strong> <span id="currentIp">-</span><br>
            <strong>SSID:</strong> <span id="currentSsid">-</span><br>
            <strong>Subnet:</strong> <span id="currentSubnet">-</span><br>
            <strong>Server Uptime:</strong> <span id="uptime">-</span>
        </div>
        
        <div class="stats">
            <div class="stat-card">
                <div class="stat-number" id="totalDevices">0</div>
                <div class="stat-label">Total Devices</div>
            </div>
            <div class="stat-card">
                <div class="stat-number" id="onlineDevices">0</div>
                <div class="stat-label">Online Now</div>
            </div>
            <div class="stat-card">
                <div class="stat-number" id="fixedIPs">0</div>
                <div class="stat-label">Fixed IPs</div>
            </div>
            <div class="stat-card">
                <div class="stat-number" id="lastScan">-</div>
                <div class="stat-label">Last Scan</div>
            </div>
        </div>
        
        <div class="tab">
            <button class="tablinks active" onclick="openTab(event, 'Devices')">Connected Devices</button>
            <button class="tablinks" onclick="openTab(event, 'Settings')">AP Settings</button>
            <button class="tablinks" onclick="openTab(event, 'System')">System</button>
        </div>
        
        <div id="Devices" class="tabcontent" style="display:block;">
            <h2>Network Devices</h2>
            <div class="device-actions">
                <button onclick="refreshDevices()">Refresh Devices</button>
                <button onclick="scanNetwork()" class="btn-warning">Scan Network</button>
            </div>
            <table>
                <thead>
                    <tr>
                        <th>Device Name</th>
                        <th>IP Address</th>
                        <th>MAC Address</th>
                        <th>Fixed IP</th>
                        <th>RSSI</th>
                        <th>Actions</th>
                    </tr>
                </thead>
                <tbody id="devicesBody"></tbody>
            </table>
            <div class="refresh-info">Auto-refreshing every 5 seconds</div>
        </div>
        
        <div id="Settings" class="tabcontent">
            <h2>Access Point Settings</h2>
            <form onsubmit="saveSettings(event)">
                <label>SSID:</label>
                <input type="text" id="settingsSsid" required>
                
                <label>Password:</label>
                <input type="password" id="settingsPassword" required minlength="8">
                
                <label>Subnet (192.168.X.1):</label>
                <input type="number" id="settingsSubnet" min="1" max="254" required>
                
                <button type="submit">Save Settings</button>
            </form>
        </div>
        
        <div id="System" class="tabcontent">
            <h2>System Settings</h2>
            <div class="warning">
                <h3>Danger Zone</h3>
                <p>Clearing EEPROM will reset all settings to factory defaults.</p>
                <button class="btn-danger" onclick="showClearConfirmation()">Clear EEPROM</button>
            </div>
        </div>
        
        <!-- Edit Device Modal -->
        <div id="editModal" class="modal">
            <div class="modal-content">
                <span style="float:right;cursor:pointer;font-size:24px;" onclick="closeModal()">×</span>
                <h3>Edit Device</h3>
                <form onsubmit="saveDeviceSettings(event)">
                    <input type="hidden" id="editMac">
                    <input type="hidden" id="editOriginalName">
                    
                    <div class="section">
                        <div class="section-title">Device Name</div>
                        <input type="text" id="editName" required maxlength="30" placeholder="Enter device name">
                    </div>
                    
                    <div class="section">
                        <div class="section-title">Comments</div>
                        <textarea id="editComment" placeholder="Enter multi-line comment for this device"></textarea>
                    </div>
                    
                    <div class="section">
                        <div class="section-title">Fixed IP Address</div>
                        <div class="checkbox-container">
                            <input type="checkbox" id="editFixedIP" onchange="toggleIPInput()">
                            <label>Enable fixed IP address</label>
                        </div>
                        
                        <div id="ipInputContainer" class="ip-input">
                            <label>IP Address (192.168.<span id="subnetPart">-</span>.x):</label>
                            <input type="number" id="editCustomIP" min="2" max="254" placeholder="e.g., 100">
                        </div>
                    </div>
                    
                    <div class="section">
                        <div class="section-title">System Attributes</div>
                        <p style="font-size: 12px; color: #666; margin-bottom: 10px;">
                            Add system attributes as key-value pairs
                        </p>
                        <div class="attributes-container" id="attributesContainer">
                            <!-- Attributes will be added here dynamically -->
                        </div>
                        <button type="button" onclick="addAttribute()" style="background: #17a2b8;">Add Attribute</button>
                    </div>
                    
                    <button type="submit">Save</button>
                    <button type="button" onclick="closeModal()" style="background:#6c757d;">Cancel</button>
                </form>
            </div>
        </div>
        
        <!-- Clear EEPROM Modal -->
        <div id="clearModal" class="modal">
            <div class="modal-content">
                <span style="float:right;cursor:pointer;font-size:24px;" onclick="closeClearModal()">×</span>
                <h3>Confirm EEPROM Clear</h3>
                <p>Are you sure you want to clear all EEPROM data? This will reset all settings to factory defaults.</p>
                <button class="btn-danger" onclick="clearEEPROM()">Yes, Clear EEPROM</button>
                <button type="button" onclick="closeClearModal()" style="background:#6c757d;">Cancel</button>
            </div>
        </div>
    </div>

    <script>
        let autoRefreshInterval = null;
        let serverStartTime = Date.now();
        let attributeCounter = 0;
        let devicesDatas = {};
        
        function openTab(evt, tabName) {
            const tabcontent = document.getElementsByClassName("tabcontent");
            for (let i = 0; i < tabcontent.length; i++) {
                tabcontent[i].style.display = "none";
            }
            
            const tablinks = document.getElementsByClassName("tablinks");
            for (let i = 0; i < tablinks.length; i++) {
                tablinks[i].className = tablinks[i].className.replace(" active", "");
            }
            
            document.getElementById(tabName).style.display = "block";
            evt.currentTarget.className += " active";
        }
        
        function formatUptime(milliseconds) {
            const seconds = Math.floor(milliseconds / 1000);
            const days = Math.floor(seconds / 86400);
            const hours = Math.floor((seconds % 86400) / 3600);
            const minutes = Math.floor((seconds % 3600) / 60);
            const secs = seconds % 60;
            
            if (days > 0) return `${days}d ${hours}h ${minutes}m`;
            if (hours > 0) return `${hours}h ${minutes}m ${secs}s`;
            if (minutes > 0) return `${minutes}m ${secs}s`;
            return `${secs}s`;
        }
        
        function updateUptime() {
            const uptimeElement = document.getElementById('uptime');
            if (uptimeElement) {
                uptimeElement.textContent = formatUptime(Date.now() - serverStartTime);
            }
        }
        
        function toggleIPInput() {
            const fixedIP = document.getElementById('editFixedIP').checked;
            document.getElementById('ipInputContainer').style.display = fixedIP ? 'block' : 'none';
        }
        
        function addAttribute(key = '', value = '') {
            const container = document.getElementById('attributesContainer');
            const attrId = `attr_${attributeCounter++}`;
            
            const attrRow = document.createElement('div');
            attrRow.className = 'attribute-row';
            attrRow.id = attrId;
            
            attrRow.innerHTML = `
                <input type="text" class="attribute-key" placeholder="Key" value="${key}" maxlength="30">
                <input type="text" class="attribute-value" placeholder="Value" value="${value}" maxlength="63">
                <button type="button" class="attribute-remove" onclick="removeAttributeDev('${attrId}')">Remove</button>
            `;
            
            container.appendChild(attrRow);
        }
        
        function removeAttributeDev(attrId) {
            const element = document.getElementById(attrId);
            if (element) {
                element.remove();
            }
        }
        
        function getAttributes() {
            const attributes = [];
            const rows = document.querySelectorAll('.attribute-row');
            
            rows.forEach(row => {
                const key = row.querySelector('.attribute-key').value.trim();
                const value = row.querySelector('.attribute-value').value.trim();
                
                if (key && value) {
                    attributes.push(`${key}:"${value}"`);
                }
            });
            
            return attributes.join('\n');
        }
        
        function escapeHtml(text) {
            const div = document.createElement('div');
            div.textContent = text;
            return div.innerHTML;
        }
        
        function refreshDevices() {
            fetch('/api/devices')
                .then(response => response.json())
                .then(data => {
                    // Update stats
                    document.getElementById('totalDevices').textContent = data.totalDevices;
                    document.getElementById('onlineDevices').textContent = data.onlineDevices;
                    document.getElementById('fixedIPs').textContent = data.fixedIPs;
                    document.getElementById('currentIp').textContent = data.espIp;
                    document.getElementById('lastScan').textContent = new Date().toLocaleTimeString();
                    
                    // Update devices table
                    const tbody = document.getElementById('devicesBody');
                    tbody.innerHTML = '';
                    
                    if (!data.devices || data.devices.length === 0) {
                        tbody.innerHTML = '<tr><td colspan="7" style="text-align:center;">No devices found</td></tr>';
                        devicesDatas = [];
                        return;
                    }
                    devicesDatas = data.devices; 
                    for (let ind = 0; ind < data.devices.length; ind++) {
                        const device = data.devices[ind];
                        const row = document.createElement('tr');
                        const statusClass = device.connected ? 'connected' : 'disconnected';
                        const rssiText = device.rssi || 'N/A';
                        
                        // Безопасное экранирование данных
                        const safeMac = escapeHtml(device.mac);
                        const safeName = escapeHtml(device.displayName);
                        const safeIp = escapeHtml(device.ip);
                        const safeComment = device.comment ? escapeHtml(device.comment) : '';
                        const safeAttributes = JSON.stringify(device.attributes || {});
                        const deviceData = JSON.stringify(device || {});
                        
                        row.innerHTML = `
                            <td>${safeName}</td>
                            <td>${safeIp}</td>
                            <td>${safeMac}</td>
                            <td>${device.ipFixed ? 'Yes' : 'No'}</td>
                            <td>${rssiText}</td>
                            <td>
                                <button onclick="editDevice(${ind})">Edit</button>
                                ${device.ipFixed ? `<button onclick="unfixIP('${safeMac}')" class="btn-warning">Unfix IP</button>` : ''}
                            </td>
                        `;
                        tbody.appendChild(row);
                    }             
                    /*       
                    data.devices.forEach(device => {
                        const row = document.createElement('tr');
                        const statusClass = device.connected ? 'connected' : 'disconnected';
                        const rssiText = device.rssi || 'N/A';
                        
                        // Безопасное экранирование данных
                        const safeMac = escapeHtml(device.mac);
                        const safeName = escapeHtml(device.displayName);
                        const safeIp = escapeHtml(device.ip);
                        const safeComment = device.comment ? escapeHtml(device.comment) : '';
                        const safeAttributes = JSON.stringify(device.attributes || {});
                        
                        row.innerHTML = `
                            <td>${safeName}</td>
                            <td>${safeIp}</td>
                            <td>${safeMac}</td>
                            <td>${device.ipFixed ? 'Yes' : 'No'}</td>
                            <td>${rssiText}</td>
                            <td>
                                <button onclick="editDevice('${safeMac}', '${safeName}', ${device.ipFixed}, '${safeIp}', '${safeComment}', ${safeAttributes})">Edit</button>
                                ${device.ipFixed ? `<button onclick="unfixIP('${safeMac}')" class="btn-warning">Unfix IP</button>` : ''}
                            </td>
                        `;
                        tbody.appendChild(row);
                    });
                    */
                })
                .catch(error => {
                    console.error('Error fetching devices:', error);
                    document.getElementById('devicesBody').innerHTML = '<tr><td colspan="7" style="text-align:center;color:red;">Error loading devices</td></tr>';
                });
        }
        
        function scanNetwork() {
            fetch('/api/scan', { method: 'POST' })
                .then(response => response.json())
                .then(data => {
                    if (data.status === 'success') {
                        refreshDevices();
                    } else {
                        alert('Scan failed: ' + data.message);
                    }
                })
                .catch(error => {
                    alert('Scan error: ' + error);
                });
        }
        
        function editDevice(ind) {
            device = devicesDatas[ind];
            let mac = escapeHtml(device.mac);
            let name = escapeHtml(device.displayName);
            let fixedIP = device.ipFixed;
            let currentIP = escapeHtml(device.ip);
            let comment =  device.comment ? escapeHtml(device.comment) : '';
            let attributes = JSON.stringify(device.attributes || {});
          
            document.getElementById('editMac').value = mac;
            document.getElementById('editOriginalName').value = name;
            document.getElementById('editName').value = name;
            document.getElementById('editFixedIP').checked = fixedIP;
            document.getElementById('editComment').value = comment;
            
            // Clear existing attributes
            document.getElementById('attributesContainer').innerHTML = '';
            
            // Add attributes from device data
            try {
                const attrObj = typeof attributes === 'string' ? JSON.parse(attributes) : attributes;
                if (attrObj && typeof attrObj === 'object') {
                    for (const [key, value] of Object.entries(attrObj)) {
                        addAttribute(key, value);
                    }
                }
            } catch (e) {
                console.error('Error parsing attributes:', e);
            }
            
            // Extract last octet from IP for editing
            let lastOctet = '';
            if (fixedIP && currentIP && currentIP !== '-') {
                const ipParts = currentIP.split('.');
                if (ipParts.length === 4) {
                    lastOctet = ipParts[3];
                }
            }
            document.getElementById('editCustomIP').value = lastOctet;
            
            // Update subnet part in modal
            const currentSubnet = document.getElementById('currentSubnet').textContent;
            if (currentSubnet !== '-') {
                document.getElementById('subnetPart').textContent = currentSubnet;
            }
            
            toggleIPInput();
            document.getElementById('editModal').style.display = 'block';
        }
        
        function closeModal() {
            document.getElementById('editModal').style.display = 'none';
        }
        
        function showClearConfirmation() {
            document.getElementById('clearModal').style.display = 'block';
        }
        
        function closeClearModal() {
            document.getElementById('clearModal').style.display = 'none';
        }
        
        function clearEEPROM() {
            fetch('/clearEEPROM')
                .then(response => {
                    if (response.ok) {
                        alert('EEPROM cleared. Rebooting...');
                        setTimeout(() => { location.reload(); }, 2000);
                    } else {
                        alert('Error clearing EEPROM');
                    }
                })
                .catch(error => alert('Error: ' + error));
        }
        
        function saveDeviceSettings(event) {
            event.preventDefault();
            
            const mac = document.getElementById('editMac').value;
            const name = document.getElementById('editName').value;
            const comment = document.getElementById('editComment').value;
            const fixedIP = document.getElementById('editFixedIP').checked;
            const customIP = fixedIP ? document.getElementById('editCustomIP').value : '';
            const attributes = getAttributes();
            
            const deviceData = new URLSearchParams();
            deviceData.append('mac', mac);
            deviceData.append('name', name);
            deviceData.append('comment', comment);
            deviceData.append('attributes', attributes);
            deviceData.append('fixedIP', fixedIP);
            if (fixedIP) {
                deviceData.append('ip', customIP);
            }
            
            fetch('/api/rename', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/x-www-form-urlencoded',
                },
                body: deviceData
            })
            .then(response => response.json())
            .then(data => {
                if (data.status === 'success') {
                    closeModal();
                    refreshDevices();
                } else {
                    alert('Error saving device: ' + data.message);
                }
            })
            .catch(error => alert('Error: ' + error));
        }
        
        function unfixIP(mac) {
            if (!confirm('Unfix IP address for this device?')) {
                return;
            }
            
            const deviceData = new URLSearchParams();
            deviceData.append('mac', mac);
            
            fetch('/api/unfixip', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/x-www-form-urlencoded',
                },
                body: deviceData
            })
            .then(response => response.json())
            .then(data => {
                if (data.status === 'success') {
                    refreshDevices();
                } else {
                    alert('Error: ' + data.message);
                }
            })
            .catch(error => alert('Error: ' + error));
        }
        
        function saveSettings(event) {
            event.preventDefault();
            
            const ssid = document.getElementById('settingsSsid').value;
            const password = document.getElementById('settingsPassword').value;
            const subnet = document.getElementById('settingsSubnet').value;
            
            const settingsData = new URLSearchParams();
            settingsData.append('ssid', ssid);
            settingsData.append('password', password);
            settingsData.append('subnet', subnet);
            
            fetch('/api/settings', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/x-www-form-urlencoded',
                },
                body: settingsData
            })
            .then(response => response.json())
            .then(data => {
                if (data.status === 'success') {
                    alert('Settings saved successfully! Rebooting...');
                    setTimeout(() => { location.reload(); }, 2000);
                } else {
                    alert('Error saving settings: ' + data.message);
                }
            })
            .catch(error => alert('Error: ' + error));
        }
        
        // Load current settings when settings tab is opened
        document.getElementById('Settings').addEventListener('click', function() {
            fetch('/api/settings')
                .then(response => response.json())
                .then(data => {
                    document.getElementById('settingsSsid').value = data.ssid || '';
                    document.getElementById('settingsSubnet').value = data.subnet || '50';
                    document.getElementById('currentSsid').textContent = data.ssid || '-';
                    document.getElementById('currentSubnet').textContent = data.subnet || '-';
                })
                .catch(error => console.error('Error loading settings:', error));
        });
        
        // Initialize
        window.onload = function() {
            refreshDevices();
            updateUptime();
            setInterval(updateUptime, 1000);
            autoRefreshInterval = setInterval(refreshDevices, 5000);
            
            // Load initial settings
            fetch('/api/settings')
                .then(response => response.json())
                .then(data => {
                    document.getElementById('currentSsid').textContent = data.ssid || '-';
                    document.getElementById('currentSubnet').textContent = data.subnet || '-';
                })
                .catch(error => console.error('Error loading settings:', error));
        };
        
        window.onclick = function(event) {
            const modal = document.getElementById('editModal');
            if (event.target === modal) closeModal();
            
            const clearModal = document.getElementById('clearModal');
            if (event.target === clearModal) closeClearModal();
        };
    </script>
</body>
</html>
)rawliteral";

// Обработчик главной страницы
void handleRoot() {
  Serial.println("HTTP: Serving web interface");
  server.send_P(200, "text/html", htmlPage);
}

// Обработчик очистки EEPROM
void handleClearEEPROM() {
  Serial.println("HTTP: Handling clear EEPROM request");
  
  // Очищаем EEPROM
  EEPROM.begin(4096);
  for (int i = 0; i < 4096; i++) {
    EEPROM.write(i, 0);
  }
  EEPROM.commit();
  EEPROM.end();
  
  // Сбрасываем настройки
  safeStrcpy(networkSettings.ssid, ap_ssid, sizeof(networkSettings.ssid));
  safeStrcpy(networkSettings.password, ap_password, sizeof(networkSettings.password));
  safeStrcpy(networkSettings.subnet, "50", sizeof(networkSettings.subnet));
  networkSettings.configured = false;
  
  // Очищаем устройства
  deviceCount = 0;
  aliasCount = 0;
  commentCount = 0;
  attributeCount = 0;
  fixedIPCount = 0;
  
  server.send(200, "text/plain", "EEPROM cleared successfully");
  
  delay(1000);
  ESP.restart();
}

// Настройка WiFi точки доступа
void setupWiFiAP() {
  Serial.println("Setting up Access Point...");
  
  WiFi.mode(WIFI_AP);
  
  // Конвертируем подсеть в число
  int subnet = atoi(networkSettings.subnet);
  if (subnet < 1 || subnet > 254) {
    subnet = 50; // Значение по умолчанию при ошибке
  }
  
  IPAddress local_ip(192, 168, subnet, 1);
  IPAddress gateway(192, 168, subnet, 1);
  IPAddress subnet_mask(255, 255, 255, 0);
  
  WiFi.softAP(networkSettings.ssid, networkSettings.password);
  WiFi.softAPConfig(local_ip, gateway, subnet_mask);
  
  Serial.println("Access Point Started");
  Serial.print("SSID: "); Serial.println(networkSettings.ssid);
  Serial.print("IP: "); Serial.println(WiFi.softAPIP());
  Serial.printf("Subnet: 192.168.%d.0/24\n", subnet);
}

void clrEeprom() {
  // Очищаем EEPROM
  EEPROM.begin(4096);
  for (int i = 0; i < 4096; i++) {
    EEPROM.write(i, 0);
  }
  EEPROM.commit();
  EEPROM.end();
}

void setup() {
  Serial.begin(115200);
  delay(3000);

  if (CLE_EEPROM==true) clrEeprom();
  
  Serial.println("\nStarting Network Monitor with Web Interface...");
  
  // Загрузка настроек сети из EEPROM
  loadNetworkSettingsFromEEPROM();
  
  // Загрузка алиасов устройств, комментариев и атрибутов из EEPROM
  loadDeviceDataFromEEPROM();
  
  // Настройка WiFi
  setupWiFiAP();
  
  // Инициализация DHCP сервера
  initDHCPServer();
  
  // Настройка маршрутов сервера с CORS поддержкой
  server.on("/", handleRoot);
  server.on("/api/devices", HTTP_GET, handleApiDevices);
  server.on("/api/devices", HTTP_OPTIONS, handleOptions);
  server.on("/api/scan", HTTP_POST, handleApiScan);
  server.on("/api/scan", HTTP_OPTIONS, handleOptions);
  server.on("/api/rename", HTTP_POST, handleApiRename);
  server.on("/api/rename", HTTP_OPTIONS, handleOptions);
  server.on("/api/fixip", HTTP_POST, handleApiFixIP);
  server.on("/api/fixip", HTTP_OPTIONS, handleOptions);
  server.on("/api/unfixip", HTTP_POST, handleApiUnfixIP);
  server.on("/api/unfixip", HTTP_OPTIONS, handleOptions);
  server.on("/api/settings", HTTP_GET, handleApiGetSettings);
  server.on("/api/settings", HTTP_OPTIONS, handleOptions);
  server.on("/api/settings", HTTP_POST, handleApiSettings);
  server.on("/clearEEPROM", handleClearEEPROM);
  
  // Запуск сервера
  server.begin();
  
  Serial.println("HTTP web interface server started on port 80");
  Serial.println("DHCP server started on port 67");
  Serial.println("Network monitor ready!");
  
  // Первое сканирование
  scanNetwork();
  
  Serial.println("Setup completed successfully!");
}

void loop() {
  server.handleClient();
  
  // Обработка DHCP запросов
  handleDHCP();
  
  if (millis() - lastScanTime >= SCAN_INTERVAL) {
    scanNetwork();
  }
  
  // Буферизованное сохранение в EEPROM
  if (eepromDirty && (millis() - lastEEPROMSave >= EEPROM_SAVE_INTERVAL)) {
    saveDeviceDataToEEPROM();
  }
  
  // Периодическая проверка восстановления
  if (millis() - lastRecoveryCheck > RECOVERY_CHECK_INTERVAL) {
    recoveryCheck();
  }
  
  delay(100);
}
