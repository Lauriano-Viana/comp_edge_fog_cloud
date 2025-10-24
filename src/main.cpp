/*
 * Sistema de Monitoramento Cardíaco Vestível - ESP32 (Versão Completa)
 * Fase 3 - Atividade 1: Edge Computing + Cloud Computing
 * * * * CORREÇÕES FINAIS E VARIAÇÕES IMPLEMENTADAS:
 * 1. Conectividade MQTT: Broker revertido para 'broker.hivemq.com' (porta 1883, sem credenciais).
 * 2. Variação do BPM: Implementada a função generateHeartRate() para variar o BPM suavemente.
 * 3. Conectividade MQTT: Cliente WiFiClient e porta 1883 mantidos.
 */

#include <WiFi.h>
#include <WiFiClient.h> // Cliente Padrão (não-seguro)
#include <DHT.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>

// ==================== CONFIGURAÇÕES DOS SENSORES ====================
#define DHT_PIN 4
#define DHT_TYPE DHT22
#define HEART_RATE_BUTTON 2 

// ==================== CONFIGURAÇÕES DOS LEDs ====================
#define WIFI_LED_PIN 5      // LED Azul - Status WiFi
#define MQTT_LED_PIN 18     // LED Verde - Status MQTT
#define ALERT_LED_PIN 19    // LED Vermelho - Alertas

// ==================== CONFIGURAÇÕES WiFi ====================
const char* ssid = "Wokwi-GUEST";
const char* password = "";

// ==================== CONFIGURAÇÕES MQTT ====================
const char* mqtt_server = "broker.hivemq.com"; // <<< CORREÇÃO: Revertido para broker.hivemq.com
const int mqtt_port = 1883;
const char* mqtt_client_id = "ESP32_Medical_001_LCV"; 
// As credenciais de login/senha foram removidas para usar a porta pública 1883

// Tópicos MQTT
const char* topic_temperature = "fiap/medical/temperature";
const char* topic_humidity = "fiap/medical/humidity";
const char* topic_heartrate = "fiap/medical/heartrate";
const char* topic_alldata = "fiap/medical/alldata";
const char* topic_alert = "fiap/medical/alert";
const char* topic_status = "fiap/medical/status";

// ==================== OBJETOS ====================
DHT dht(DHT_PIN, DHT_TYPE);
WiFiClient espClient; 
PubSubClient mqttClient(espClient);

// ==================== VARIÁVEIS DE CONTROLE ====================
bool wifiConnected = false;
bool mqttConnected = false;
bool littleFSMounted = false;
unsigned long lastSensorRead = 0;
const unsigned long SENSOR_INTERVAL = 5000; // 5 segundos entre leituras

// Variável para BPM e controle de variação
int heartRate = 70; // BPM inicial
unsigned long lastHeartRateUpdate = 0;
const unsigned long HR_UPDATE_INTERVAL = 10000; // Varia BPM a cada 10s

// Simulação de conexão WiFi alternada
unsigned long lastWifiToggle = 0;
const unsigned long WIFI_TOGGLE_INTERVAL = 45000; // Alternar a cada 45s

// ==================== CONFIGURAÇÕES DE ARMAZENAMENTO ====================
const int MAX_STORED_READINGS = 1000; // Limite de amostras offline

// Estrutura para dados dos sensores
struct SensorData {
  float temperature;
  float humidity;
  int heartRate;
  unsigned long timestamp;
  bool sent;
};

// Buffer para dados offline
SensorData offlineBuffer[MAX_STORED_READINGS];
int bufferIndex = 0;
int totalStored = 0;

// ==================== DECLARAÇÃO DE FUNÇÕES (PROTÓTIPOS) ====================
void setupWiFi();
void checkWiFiConnection();
void setupMQTT();
void reconnectMQTT();
void mqttCallback(char* topic, byte* payload, unsigned int length);
void readSensors();
int generateHeartRate(); 
void storeData(SensorData data);
void saveToLittleFS(SensorData data);
void loadOfflineData();
void syncOfflineData();
bool sendDataToCloud(SensorData data);
void checkAlerts(SensorData data);
void clearOfflineData();
void testLEDs();
void blinkMQTTLED();
void printFileSystemInfo();

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  delay(1000);
  randomSeed(analogRead(0)); 
  
  Serial.println("\n\n");
  Serial.println("╔════════════════════════════════════════════════════════╗");
  Serial.println("║   SISTEMA DE MONITORAMENTO CARDÍACO VESTÍVEL v2.2     ║");
  Serial.println("║       ESP32 + Edge/Cloud Computing + Variação BPM      ║");
  Serial.println("╚════════════════════════════════════════════════════════╝");
  
  // Inicializar sensores
  dht.begin();
  pinMode(HEART_RATE_BUTTON, INPUT_PULLUP);
  
  // Configurar LEDs
  pinMode(WIFI_LED_PIN, OUTPUT);
  pinMode(MQTT_LED_PIN, OUTPUT);
  pinMode(ALERT_LED_PIN, OUTPUT);
  
  digitalWrite(WIFI_LED_PIN, LOW);
  digitalWrite(MQTT_LED_PIN, LOW);
  digitalWrite(ALERT_LED_PIN, LOW);
  
  // Inicializar LittleFS
  Serial.println("\n📁 Inicializando LittleFS...");
  littleFSMounted = LittleFS.begin(false);
  
  if (!littleFSMounted) {
    Serial.println("⚠️  Falha na montagem inicial, tentando formatar...");
    littleFSMounted = LittleFS.begin(true);
  }
  
  if (littleFSMounted) {
    Serial.println("✅ LittleFS montado com sucesso");
    printFileSystemInfo();
  } else {
    Serial.println("❌ LittleFS não disponível");
    Serial.println("ℹ️  Sistema funcionará apenas com armazenamento em RAM");
  }
  
  // Carregar dados offline salvos
  loadOfflineData();
  
  // Configurar WiFi
  setupWiFi();
  
  // Configurar MQTT
  setupMQTT();
  
  testLEDs();
  
  Serial.println("\n╔════════════════════════════════════════════════════════╗");
  Serial.println("║              SISTEMA INICIADO COM SUCESSO              ║");
  Serial.println("╚════════════════════════════════════════════════════════╝");
  Serial.println("\n📋 INSTRUÇÕES:");
  Serial.println("  📊 Dados serão coletados a cada 5 segundos");
  Serial.println("  🔄 WiFi alternará ON/OFF a cada 45s (demonstração)");
  Serial.println("  💾 Dados offline serão sincronizados ao reconectar\n");
}

// ==================== LOOP PRINCIPAL ====================
void loop() {
  // Verificar conectividade WiFi (simulação alternada)
  checkWiFiConnection();
  
  // Manter conexão MQTT ativa
  if (wifiConnected) {
    if (!mqttClient.connected()) {
      reconnectMQTT();
    } else {
      mqttClient.loop();
    }
  }
  
  // Variação do BPM
  if (millis() - lastHeartRateUpdate >= HR_UPDATE_INTERVAL) {
      heartRate = generateHeartRate();
      lastHeartRateUpdate = millis();
  }
  
  // Ler sensores periodicamente
  if (millis() - lastSensorRead >= SENSOR_INTERVAL) {
    readSensors();
    lastSensorRead = millis();
  }
  
  // Tentar sincronizar dados offline
  if (wifiConnected && mqttConnected && totalStored > 0) {
    syncOfflineData();
  }
  
  delay(100);
}

// ==================== VARIAÇÃO SIMULADA DE BPM ====================
int generateHeartRate() {
    int minBPM = 68;
    int maxBPM = 75;
    
    // Adicionar uma chance de pico (simular atividade/stress)
    if (random(100) < 5) { // 5% de chance de pico
        minBPM = 100;
        maxBPM = 115;
    }
    
    // Gera um valor aleatório dentro do range
    int newHR = random(minBPM, maxBPM + 1);
    
    // Suavizar a transição para parecer mais orgânico
    if (abs(newHR - heartRate) > 5) {
        newHR = (newHR + heartRate) / 2;
    }
    
    Serial.print("\n✨ BPM Variado: ");
    Serial.print(newHR);
    Serial.println(" bpm");
    
    return newHR;
}

// ==================== INFORMAÇÕES DO SISTEMA DE ARQUIVOS ====================
void printFileSystemInfo() {
  // Verificar se está montado antes de consultar
  if (!littleFSMounted) {
    Serial.println("ℹ️  LittleFS não montado - usando apenas RAM");
    return;
  }
  
  Serial.println("\n📊 Informações do LittleFS:");
  Serial.print("   Total: ");
  Serial.print(LittleFS.totalBytes() / 1024);
  Serial.println(" KB");
  Serial.print("   Usado: ");
  Serial.print(LittleFS.usedBytes() / 1024);
  Serial.println(" KB");
  Serial.print("   Livre: ");
  Serial.print((LittleFS.totalBytes() - LittleFS.usedBytes()) / 1024);
  Serial.println(" KB");
}

// ==================== CONFIGURAÇÃO WiFi ====================
void setupWiFi() {
  Serial.println("\n📡 Configurando WiFi...");
  Serial.println("═══════════════════════════════════");
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  Serial.print("Conectando");
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    digitalWrite(WIFI_LED_PIN, HIGH);
    Serial.println(" ✅");
    Serial.print("📶 IP: ");
    Serial.println(WiFi.localIP());
    Serial.println("🔵 LED Azul: WiFi conectado");
    Serial.println("\n⚠️  MODO DEMONSTRAÇÃO ATIVADO:");
    Serial.println("   WiFi alternará entre ONLINE/OFFLINE");
  } else {
    Serial.println(" ❌");
    Serial.println("⚠️  WiFi não conectado - operando offline");
    wifiConnected = false;
  }
  
  lastWifiToggle = millis();
}

// ==================== VERIFICAÇÃO WiFi COM ALTERNÂNCIA ====================
void checkWiFiConnection() {
  if (millis() - lastWifiToggle >= WIFI_TOGGLE_INTERVAL) {
    wifiConnected = !wifiConnected;
    
    if (wifiConnected) {
      digitalWrite(WIFI_LED_PIN, HIGH);
      Serial.println("\n╔════════════════════════════════════════════════════════╗");
      Serial.println("║        🟢 WiFi RECONECTADO - VOLTOU ONLINE            ║");
      Serial.println("╚════════════════════════════════════════════════════════╝");
      Serial.print("📦 Dados pendentes para sincronizar: ");
      Serial.println(totalStored);
    } else {
      digitalWrite(WIFI_LED_PIN, LOW);
      digitalWrite(MQTT_LED_PIN, LOW);
      mqttConnected = false;
      Serial.println("\n╔════════════════════════════════════════════════════════╗");
      Serial.println("║        🔴 WiFi DESCONECTADO - OPERANDO OFFLINE        ║");
      Serial.println("╚════════════════════════════════════════════════════════╝");
      Serial.println("💾 Dados sendo armazenados localmente...");
    }
    
    lastWifiToggle = millis();
  }
}

// ==================== CONFIGURAÇÃO MQTT ====================
void setupMQTT() {
  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setCallback(mqttCallback);
  Serial.println("\n🌐 MQTT configurado:");
  Serial.print("   Broker: ");
  Serial.println(mqtt_server);
  Serial.print("   Porta: ");
  Serial.println(mqtt_port);
}

// ==================== RECONEXÃO MQTT ====================
void reconnectMQTT() {
  if (!wifiConnected) {
    mqttConnected = false;
    return;
  }
  
  if (!mqttClient.connected()) {
    Serial.print("\n🔄 Conectando ao MQTT... ");
    
    // Tentar limpar a conexão antes de conectar
    mqttClient.disconnect();
    
    // Tentar conexão simples com ID (esperado para HiveMQ:1883)
    if (mqttClient.connect(mqtt_client_id)) { 
      
      Serial.println("✅ Conectado!");
      mqttConnected = true;
      digitalWrite(MQTT_LED_PIN, HIGH);
      
      // Publicar status online
      mqttClient.publish(topic_status, "{\"status\":\"online\",\"device\":\"ESP32_Medical_001\"}");
      
      Serial.println("🟢 LED Verde: MQTT ativo");
    } else {
      Serial.print("❌ Falha (rc=");
      Serial.print(mqttClient.state());
      Serial.println("). Nova tentativa em breve.");
      mqttConnected = false;
      digitalWrite(MQTT_LED_PIN, LOW);
    }
  }
}

// ==================== CALLBACK MQTT ====================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("📥 Mensagem recebida [");
  Serial.print(topic);
  Serial.print("]: ");
  for (unsigned int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
}

// ==================== LEITURA DOS SENSORES ====================
void readSensors() {
  Serial.println("\n┌────────────────────────────────────────────┐");
  Serial.println("│        📊 LEITURA DOS SENSORES             │");
  Serial.println("└────────────────────────────────────────────┘");
  
  // Ler DHT22
  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();
  
  if (isnan(temperature) || isnan(humidity)) {
    Serial.println("❌ Erro na leitura do DHT22");
    return;
  }
  
  // Criar estrutura de dados
  SensorData data;
  data.temperature = temperature;
  data.humidity = humidity;
  data.heartRate = heartRate; // Usa o valor simulado e variado
  data.timestamp = millis();
  data.sent = false;
  
  // Exibir dados
  Serial.print("🌡️  Temperatura: ");
  Serial.print(temperature, 1);
  Serial.println(" °C");
  
  Serial.print("💧 Umidade: ");
  Serial.print(humidity, 1);
  Serial.println(" %");
  
  Serial.print("💓 Frequência Cardíaca: ");
  Serial.print(heartRate);
  Serial.println(" bpm");
  
  Serial.print("📡 Status: ");
  if (wifiConnected && mqttConnected) {
    Serial.println("🟢 ONLINE - Enviando para nuvem");
  } else {
    Serial.println("🔴 OFFLINE - Armazenando localmente");
  }
  
  // Armazenar dados localmente
  storeData(data);
  
  // Enviar para nuvem se conectado
  if (wifiConnected && mqttConnected) {
    sendDataToCloud(data);
  }
  
  Serial.println("└────────────────────────────────────────────┘\n");
}

// ==================== ARMAZENAR DADOS ====================
void storeData(SensorData data) {
  if (bufferIndex < MAX_STORED_READINGS) {
    offlineBuffer[bufferIndex] = data;
    bufferIndex++;
    totalStored++;
  } else {
    bufferIndex = 0;
    offlineBuffer[bufferIndex] = data;
    bufferIndex++;
    totalStored = MAX_STORED_READINGS;
  }
  
  saveToLittleFS(data);
  
  Serial.print("💾 Armazenado localmente | Total offline: ");
  Serial.print(totalStored);
  Serial.print(" / ");
  Serial.println(MAX_STORED_READINGS);
}

// ==================== SALVAR NO LITTLEFS ====================
void saveToLittleFS(SensorData data) {
  if (!littleFSMounted) {
    return;
  }
  
  File file = LittleFS.open("/sensor_data.json", FILE_APPEND);
  if (!file) {
    file = LittleFS.open("/sensor_data.json", FILE_WRITE);
    if (!file) {
      littleFSMounted = false;
      return;
    }
  }
  
  DynamicJsonDocument doc(512);
  doc["temp"] = data.temperature;
  doc["hum"] = data.humidity;
  doc["hr"] = data.heartRate;
  doc["ts"] = data.timestamp;
  doc["sent"] = data.sent;
  
  serializeJson(doc, file);
  file.println();
  file.close();
}

// ==================== CARREGAR DADOS OFFLINE ====================
void loadOfflineData() {
  Serial.println("\n📂 Carregando dados offline...");
  
  if (!littleFSMounted) {
    Serial.println("ℹ️  LittleFS não disponível - iniciando buffer vazio");
    return;
  }
  
  File file = LittleFS.open("/sensor_data.json", FILE_READ);
  if (!file) {
    Serial.println("ℹ️  Nenhum arquivo de dados encontrado (primeira execução)");
    return;
  }
  
  totalStored = 0;
  bufferIndex = 0;
  
  while (file.available() && totalStored < MAX_STORED_READINGS) {
    String line = file.readStringUntil('\n');
    
    if (line.length() > 0) {
      DynamicJsonDocument doc(512);
      DeserializationError error = deserializeJson(doc, line);
      
      if (!error && !doc["sent"].as<bool>()) {
        offlineBuffer[totalStored].temperature = doc["temp"];
        offlineBuffer[totalStored].humidity = doc["hum"];
        offlineBuffer[totalStored].heartRate = doc["hr"];
        offlineBuffer[totalStored].timestamp = doc["ts"];
        offlineBuffer[totalStored].sent = false;
        
        totalStored++;
      }
    }
  }
  
  file.close();
  bufferIndex = totalStored;
  
  if (totalStored > 0) {
    Serial.print("✅ Carregados ");
    Serial.print(totalStored);
    Serial.println(" registros pendentes");
  } else {
    Serial.println("✅ Buffer inicializado vazio");
  }
}

// ==================== SINCRONIZAR DADOS OFFLINE ====================
void syncOfflineData() {
  static int syncIndex = 0;
  static unsigned long lastSync = 0;
  
  if (millis() - lastSync >= 2000 && syncIndex < totalStored) {
    Serial.println("\n🔄 ═══════════════════════════════════════");
    Serial.println("   SINCRONIZANDO DADOS OFFLINE");
    Serial.println("   ═══════════════════════════════════════");
    
    SensorData data = offlineBuffer[syncIndex];
    
    if (sendDataToCloud(data)) {
      offlineBuffer[syncIndex].sent = true;
      syncIndex++;
      
      Serial.print("   ✅ Registro ");
      Serial.print(syncIndex);
      Serial.print(" de ");
      Serial.print(totalStored);
      Serial.println(" sincronizado");
    }
    
    lastSync = millis();
  }
  
  if (syncIndex >= totalStored && totalStored > 0) {
    clearOfflineData();
    syncIndex = 0;
    totalStored = 0;
    bufferIndex = 0;
    
    Serial.println("\n╔════════════════════════════════════════════════════════╗");
    Serial.println("║   ✅ SINCRONIZAÇÃO COMPLETA - BUFFER LIMPO            ║");
    Serial.println("╚════════════════════════════════════════════════════════╝\n");
  }
}

// ==================== ENVIAR DADOS PARA NUVEM ====================
bool sendDataToCloud(SensorData data) {
  if (!wifiConnected || !mqttConnected) {
    return false;
  }
  
  blinkMQTTLED();
  
  Serial.println("\n📡 ═══════════════════════════════════════");
  Serial.println("   TRANSMISSÃO MQTT PARA NUVEM");
  Serial.println("   ═══════════════════════════════════════");
  
  // Publicar dados individuais
  String tempStr = String(data.temperature, 1);
  String humStr = String(data.humidity, 1);
  String hrStr = String(data.heartRate);
  
  bool success = true;
  success &= mqttClient.publish(topic_temperature, tempStr.c_str());
  success &= mqttClient.publish(topic_humidity, humStr.c_str());
  success &= mqttClient.publish(topic_heartrate, hrStr.c_str());
  
  Serial.println("   📤 Tópicos publicados:");
  Serial.println("      • " + String(topic_temperature));
  Serial.println("      • " + String(topic_humidity));
  Serial.println("      • " + String(topic_heartrate));
  
  // Publicar JSON completo
  DynamicJsonDocument doc(1024);
  doc["device_id"] = mqtt_client_id;
  doc["temperature"] = data.temperature;
  doc["humidity"] = data.humidity;
  doc["heartRate"] = data.heartRate; // Chave CORRIGIDA para Node-RED
  doc["timestamp"] = data.timestamp;
  doc["battery"] = 85;
  doc["rssi"] = WiFi.RSSI();
  
  String payload;
  serializeJson(doc, payload);
  
  success &= mqttClient.publish(topic_alldata, payload.c_str());
  
  Serial.println("   📦 Payload JSON:");
  Serial.println("      " + payload);
  
  // Verificar alertas
  checkAlerts(data);
  
  Serial.println("   ═══════════════════════════════════════");
  Serial.println(success ? "   ✅ TRANSMISSÃO CONCLUÍDA\n" : "   ❌ FALHA NA TRANSMISSÃO\n");
  
  return success;
}

// ==================== VERIFICAR ALERTAS ====================
void checkAlerts(SensorData data) {
  Serial.println("\n🔔 ═══════════════════════════════════════");
  Serial.println("   VERIFICAÇÃO DE ALERTAS");
  Serial.println("   ═══════════════════════════════════════");
  
  bool hasAlert = false;
  String alertMsg = "";
  
  // Verificar temperatura
  if (data.temperature > 38) {
    alertMsg += "🚨 CRÍTICO: Temperatura alta (" + String(data.temperature, 1) + "°C) ";
    Serial.println("   🚨 Temperatura CRÍTICA: " + String(data.temperature, 1) + "°C");
    hasAlert = true;
  } else if (data.temperature > 37.5) {
    alertMsg += "⚠️  ATENÇÃO: Temperatura elevada (" + String(data.temperature, 1) + "°C) ";
    Serial.println("   ⚠️  Temperatura ELEVADA: " + String(data.temperature, 1) + "°C");
    hasAlert = true;
  }
  
  // Verificar frequência cardíaca
  if (data.heartRate > 120) {
    alertMsg += "🚨 CRÍTICO: FC alta (" + String(data.heartRate) + " bpm)";
    Serial.println("   🚨 Frequência Cardíaca CRÍTICA: " + String(data.heartRate) + " bpm");
    hasAlert = true;
  } else if (data.heartRate > 100 && data.heartRate <= 120) {
    alertMsg += "⚠️  ATENÇÃO: FC elevada (" + String(data.heartRate) + " bpm)";
    Serial.println("   ⚠️  Frequência Cardíaca ELEVADA: " + String(data.heartRate) + " bpm");
    hasAlert = true;
  }
  
  // Controlar LED vermelho e publicar alerta
  if (hasAlert) {
    digitalWrite(ALERT_LED_PIN, HIGH);
    Serial.println("   🔴 LED Vermelho: ALERTA ATIVO");
    
    // Publicar alerta via MQTT
    if (mqttConnected) {
      DynamicJsonDocument alertDoc(512);
      alertDoc["device_id"] = mqtt_client_id;
      alertDoc["alert_level"] = (data.temperature > 38 || data.heartRate > 120) ? "CRITICAL" : "WARNING";
      alertDoc["message"] = alertMsg;
      alertDoc["timestamp"] = millis();
      
      String alertPayload;
      serializeJson(alertDoc, alertPayload);
      mqttClient.publish(topic_alert, alertPayload.c_str());
      
      Serial.println("   📢 Alerta publicado via MQTT");
    }
  } else {
    digitalWrite(ALERT_LED_PIN, LOW);
    Serial.println("   ✅ Todos os parâmetros normais");
  }
  
  Serial.println("   ═══════════════════════════════════════\n");
}

// ==================== LIMPAR DADOS OFFLINE ====================
void clearOfflineData() {
  if (!littleFSMounted) {
    Serial.println("ℹ️  Dados limpos do buffer RAM");
    return;
  }
  
  if (LittleFS.remove("/sensor_data.json")) {
    Serial.println("🗑️  Dados limpos de RAM + LittleFS");
  } else {
    Serial.println("ℹ️  Buffer RAM limpo");
  }
}

// ==================== TESTE DOS LEDs ====================
void testLEDs() {
  Serial.println("\n🔧 ═══════════════════════════════════════");
  Serial.println("   TESTE DOS LEDs");
  Serial.println("   ═══════════════════════════════════════");
  
  Serial.println("   🔵 Testando LED Azul (WiFi)...");
  digitalWrite(WIFI_LED_PIN, HIGH);
  delay(500);
  digitalWrite(WIFI_LED_PIN, LOW);
  delay(500);
  
  Serial.println("   🟢 Testando LED Verde (MQTT)...");
  digitalWrite(MQTT_LED_PIN, HIGH);
  delay(500);
  digitalWrite(MQTT_LED_PIN, LOW);
  delay(500);
  
  Serial.println("   🔴 Testando LED Vermelho (Alertas)...");
  digitalWrite(ALERT_LED_PIN, HIGH);
  delay(500);
  digitalWrite(ALERT_LED_PIN, LOW);
  delay(500);
  
  Serial.println("   ✅ Teste concluído!");
  Serial.println("   ═══════════════════════════════════════\n");
}

// ==================== PISCAR LED MQTT ====================
void blinkMQTTLED() {
  for (int i = 0; i < 3; i++) {
    digitalWrite(MQTT_LED_PIN, HIGH);
    delay(100);
    digitalWrite(MQTT_LED_PIN, LOW);
    delay(100);
  }
  digitalWrite(MQTT_LED_PIN, HIGH); // Manter aceso
}