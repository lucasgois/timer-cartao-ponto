/**
 * Lucas Alves de Gois
 * Heltec LoRa 32 V2
 */

#include <WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <U8g2lib.h>  // Biblioteca U8g2 para o OLED
#include <Wire.h>     // Necessário para I2C do OLED
#include "config.h"

// --- Configurações do NTP (Servidor de Tempo) ---
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");
const long gmtOffset_sec = -10800;  // GMT-3 (offset em segundos para o Brasil)

// --- Configurações de Pinos e Pulso ---
const int onboardLedPin = 25;  // LED integrado do Heltec LoRa 32 (geralmente GPIO 25)
const int buzzerPin = 12;      // Pino GPIO onde o aviso sonoro (sirene) está conectado

const int pulsoAgendadoDuracao = 3000;       // Duração do pulso agendado em milissegundos para o buzzer (alarme)
const int pulsoWifiConectadoDuracao = 2000;  // Duração do pulso para o LED ao conectar no Wi-Fi
const int pulsoWifiErroCurtoDuracao = 300;   // Duração de cada pulso curto para o LED em caso de erro no Wi-Fi

// --- Horários de Acionamento (HH:MM) ---
struct HorarioAcionamento {
  int hora;
  int minuto;
  bool acionadoHoje;
};

HorarioAcionamento horarios[] = {
  { 8, 0, false },
  { 12, 0, false },
  { 13, 12, false },
  { 18, 0, false }
};
const int numHorarios = sizeof(horarios) / sizeof(horarios[0]);

unsigned long ultimoTempoVerificado = 0;
const unsigned long intervaloVerificacao = 1000;  // Verifica a cada 1 segundo

// --- Configurações do OLED com U8g2 ---
#define OLED_SDA 4
#define OLED_SCL 15
#define OLED_RST 16

U8G2_SSD1306_128X64_NONAME_F_SW_I2C u8g2(U8G2_R0, OLED_SCL, OLED_SDA, OLED_RST);

// --- Funções de Ajuda ---

// Função genérica para piscar um pino
void piscarPino(int pin, int duracaoHIGH, int duracaoLOW, int numPulsos) {
  for (int i = 0; i < numPulsos; i++) {
    digitalWrite(pin, HIGH);
    delay(duracaoHIGH);
    digitalWrite(pin, LOW);
    if (i < numPulsos - 1) {  // Só espera entre os pulsos
      delay(duracaoLOW);
    }
  }
}

// Função para exibir mensagem no OLED (uma linha)
void tela(String mensagem) {
  Serial.print("tela: ");
  Serial.println(mensagem);

  u8g2.clearBuffer();
  u8g2.drawStr(0, 32, mensagem.c_str());
  u8g2.sendBuffer();
}

void telaDuasLinhas(String linha1, String linha2) {
  Serial.println("tela: " + linha1 + " / " + linha2);
  u8g2.clearBuffer();
  u8g2.drawStr(0, 16, linha1.c_str());  // Primeira linha (y=16)
  u8g2.drawStr(0, 32, linha2.c_str());  // Segunda linha (y=32)
  u8g2.sendBuffer();
}

// Função para encontrar o próximo horário agendado
String getProximoHorario(int currentHour, int currentMinute) {
  int proximaHora = -1;
  int proximoMinuto = -1;
  bool encontrado = false;

  // Percorre os horários para encontrar o próximo do dia
  for (int i = 0; i < numHorarios; i++) {
    // Só considera horários que ainda não foram acionados hoje
    if (!horarios[i].acionadoHoje) {
      if (horarios[i].hora > currentHour || (horarios[i].hora == currentHour && horarios[i].minuto > currentMinute)) {
        // Se este é o primeiro horário futuro encontrado, ou é mais cedo que o "próximo" já encontrado
        if (!encontrado || horarios[i].hora < proximaHora || (horarios[i].hora == proximaHora && horarios[i].minuto < proximoMinuto)) {
          proximaHora = horarios[i].hora;
          proximoMinuto = horarios[i].minuto;
          encontrado = true;
        }
      }
    }
  }

  if (encontrado) {
    return "Prox: " + String(proximaHora < 10 ? "0" : "") + String(proximaHora) + ":" + String(proximoMinuto < 10 ? "0" : "") + String(proximoMinuto);
  } else {
    // Se todos os horários do dia já foram acionados, exibe o primeiro horário do dia seguinte
    if (numHorarios > 0) {
      return "Prox: " + String(horarios[0].hora < 10 ? "0" : "") + String(horarios[0].hora) + ":" + String(horarios[0].minuto < 10 ? "0" : "") + String(horarios[0].minuto) + " (Amanha)";
    } else {
      return "Nenhum horario";
    }
  }
}

bool conectarWiFi() {
  WiFi.begin(ssid, password);

  int tentativasWifi = 0;
  while (WiFi.status() != WL_CONNECTED) {
    digitalWrite(onboardLedPin, HIGH);
    delay(100);
    digitalWrite(onboardLedPin, LOW);
    delay(400);
    Serial.print(".");

    tentativasWifi++;
    if (tentativasWifi > 40) {
      return false;
    }
  }
  return true;
}

void setup() {
  Serial.begin(9600);
  delay(100);  // Adicionar um pequeno delay inicial pode ajudar na estabilização

  pinMode(onboardLedPin, OUTPUT);    // Configura o LED integrado
  digitalWrite(onboardLedPin, LOW);  // Garante que o LED comece desligado

  pinMode(buzzerPin, OUTPUT);    // Configura o pino do aviso sonoro (sirene)
  digitalWrite(buzzerPin, LOW);  // Garante que o buzzer comece desligado

  // Inicializa a comunicação I2C para o OLED
  Wire.begin(OLED_SDA, OLED_SCL);
  u8g2.begin();
  u8g2.setFont(u8g2_font_ncenB08_tr);  // Define a fonte

  tela("SEGURSAT TECH");
  delay(1000);

  telaDuasLinhas("Conectando a", String(ssid));

  if (conectarWiFi()) {
    // Sucesso na conexão
    Serial.println("\nWiFi conectado!");
    Serial.print("Endereço IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("Endereço MAC: ");
    Serial.println(WiFi.macAddress());

    telaDuasLinhas("WiFi Conectado!", "IP: " + WiFi.localIP().toString());
    delay(2000);

    // Pulso de LED para confirmar
    digitalWrite(onboardLedPin, HIGH);
    delay(pulsoWifiConectadoDuracao);
    digitalWrite(onboardLedPin, LOW);

    timeClient.begin();
    timeClient.setTimeOffset(gmtOffset_sec);
    tela("NTP Iniciado");
    delay(1000);
  } else {
    // Falha na conexão
    Serial.println("\nFalha ao conectar ao WiFi.");
    tela("Erro no Wi-Fi!");
    piscarPino(onboardLedPin, pulsoWifiErroCurtoDuracao, pulsoWifiErroCurtoDuracao, 3);
  }
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    timeClient.update();
    unsigned long tempoAtualMillis = millis();
    if (tempoAtualMillis - ultimoTempoVerificado >= intervaloVerificacao) {
      ultimoTempoVerificado = tempoAtualMillis;
      int horaAtual = timeClient.getHours();
      int minutoAtual = timeClient.getMinutes();
      int segundoAtual = timeClient.getSeconds();
      int diaDaSemana = timeClient.getDay();  // Obtém o dia da semana (0=Dom, 1=Seg, ..., 6=Sab)

      // Display current time and next scheduled time on OLED
      String horaStr = String(horaAtual < 10 ? "0" : "") + String(horaAtual) + ":" + String(minutoAtual < 10 ? "0" : "") + String(minutoAtual) + ":" + String(segundoAtual < 10 ? "0" : "") + String(segundoAtual);
      String proximoHorarioStr = getProximoHorario(horaAtual, minutoAtual);  // Esta função ainda não considera o dia da semana para "próximo"

      u8g2.clearBuffer();
      u8g2.drawStr(0, 16, ("Hora: " + horaStr).c_str());
      u8g2.drawStr(0, 32, proximoHorarioStr.c_str());

      // Opcional: exibir o dia da semana para debug ou informação
      String diaStr;
      switch (diaDaSemana) {
        case 0: diaStr = "Dom"; break;
        case 1: diaStr = "Seg"; break;
        case 2: diaStr = "Ter"; break;
        case 3: diaStr = "Qua"; break;
        case 4: diaStr = "Qui"; break;
        case 5: diaStr = "Sex"; break;
        case 6: diaStr = "Sab"; break;
        default: diaStr = "???"; break;
      }
      u8g2.drawStr(0, 48, ("Dia: " + diaStr).c_str());  // Exibe o dia da semana na terceira linha
      u8g2.sendBuffer();


      // Reset de acionamento no início do dia
      if (horaAtual == 0 && minutoAtual == 0 && segundoAtual < 5) {  // Reset within the first 5 seconds of midnight
        for (int i = 0; i < numHorarios; i++) {
          if (horarios[i].acionadoHoje) {
            Serial.print("Resetando flag para horario: ");
            Serial.print(horarios[i].hora);
            Serial.print(":");
            Serial.print(horarios[i].minuto);
            Serial.println(" (acionadoHoje)");
            tela("Reset Horarios");
            delay(500);
            horarios[i].acionadoHoje = false;  // Resetar a flag
          }
        }
      }

      // *** LÓGICA DE ACIONAMENTO DO ALARME APENAS EM DIAS DE SEMANA ***
      // Dias da semana: 1 (Segunda) a 5 (Sexta)
      if (diaDaSemana >= 1 && diaDaSemana <= 5) {
        for (int i = 0; i < numHorarios; i++) {
          if (horaAtual == horarios[i].hora && minutoAtual == horarios[i].minuto && !horarios[i].acionadoHoje) {
            Serial.print("Acionando aviso sonoro (sirene) as ");
            Serial.print(horarios[i].hora);
            Serial.print(":");
            Serial.print(horarios[i].minuto);
            Serial.println(" (Pulso Agendado de 3s)");

            telaDuasLinhas("Alarme:", String(horarios[i].hora) + ":" + String(horarios[i].minuto));
            // Alarme: 1 pulso longo de 3 segundos
            digitalWrite(buzzerPin, HIGH);
            delay(pulsoAgendadoDuracao);
            digitalWrite(buzzerPin, LOW);

            horarios[i].acionadoHoje = true;
            Serial.println("Pulso agendado concluido.");
            tela("Pulso Concluido!");
            delay(1000);  // Mantém a mensagem no OLED por um tempo
            break;        // Sai do loop após acionar um alarme
          }
        }
      } else {
        // Opcional: Mensagem no Serial Monitor se estiver em fim de semana e não tocar
        // Serial.println("Fim de semana, alarmes desativados.");
      }
    }
  } else {
    // Se o WiFi está desconectado, tenta reconectar
    tela("WiFi Desconectado!");
    Serial.println("WiFi desconectado. Tentando reconectar...");
    // Reutiliza a função de conexão
    if (conectarWiFi()) {  // Esta função já lida com o LED e a tela
      tela("WiFi Reconectado!");
      Serial.println("\nWiFi reconectado!");
      Serial.print("Endereço IP: ");
      Serial.println(WiFi.localIP());

      Serial.print("Endereço MAC: ");
      Serial.println(WiFi.macAddress());

      timeClient.begin();  // Re-initialize NTP client
      timeClient.setTimeOffset(gmtOffset_sec);
      delay(1000);
    } else {
      tela("Falha Reconectar");
      // O pulso de erro (3 curtos) já foi dado dentro de conectarWiFi()
      delay(5000);  // Long delay if reconnection fails
    }
  }
}

// Função auxiliar para imprimir o tipo de criptografia
void printEncryptionType(wifi_auth_mode_t encryptionType) {
  switch (encryptionType) {
    case WIFI_AUTH_OPEN:
      Serial.print("Aberta");
      break;
    case WIFI_AUTH_WEP:
      Serial.print("WEP");
      break;
    case WIFI_AUTH_WPA_PSK:
      Serial.print("WPA_PSK");
      break;
    case WIFI_AUTH_WPA2_PSK:
      Serial.print("WPA2_PSK");
      break;
    case WIFI_AUTH_WPA_WPA2_PSK:
      Serial.print("WPA_WPA2_PSK");
      break;
    case WIFI_AUTH_WPA2_ENTERPRISE:
      Serial.print("WPA2_ENTERPRISE");
      break;
    case WIFI_AUTH_WPA3_PSK:
      Serial.print("WPA3_PSK");
      break;
    case WIFI_AUTH_WPA2_WPA3_PSK:
      Serial.print("WPA2_WPA3_PSK");
      break;
    default:
      Serial.print("Desconhecida");
  }
}