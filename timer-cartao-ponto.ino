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
#include "display.h"

// --- Configurações do NTP (Servidor de Tempo) ---
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");
const long gmtOffset_sec = -10800;  // GMT-3 (offset em segundos para o Brasil)

// --- Configurações de Pinos e Pulso ---
const int onboardLedPin = 25;  // LED integrado do Heltec LoRa 32 (geralmente GPIO 25)
const int buzzerPin = 12;      // Pino GPIO onde o aviso sonoro (sirene) está conectado

const int pulsoAgendadoDuracao = 3000;       // Duração do pulso agendado em milissegundos para o buzzer (alarme)
const int pulsoWifiConectadoDuracao = 1000;  // Duração do pulso para o LED ao conectar no Wi-Fi
const int pulsoWifiErroCurtoDuracao = 150;   // Duração de cada pulso curto para o LED em caso de erro no Wi-Fi

// --- Variáveis para controle de reconexão WiFi (NÃO-BLOQUEANTE)
unsigned long ultimaTentativaWifi = 0;
const unsigned long intervaloReconexaoWifi = 5000;  // Tenta reconectar a cada 10 segundos

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
void displayUmaLinha(String mensagem) {
  Serial.print("[DISPLAY] -> ");
  Serial.println(mensagem);

  u8g2.clearBuffer();
  u8g2.drawStr(0, 24, mensagem.c_str());
  u8g2.sendBuffer();
}

void displayDuasLinhas(String linha1, String linha2) {
  Serial.println("[DISPLAY] -> " + linha1 + " / " + linha2);
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
    if (!horarios[i].acionadoHoje) {
      if (horarios[i].hora > currentHour || (horarios[i].hora == currentHour && horarios[i].minuto > currentMinute)) {
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
    if (numHorarios > 0) {
      return "Prox: " + String(horarios[0].hora < 10 ? "0" : "") + String(horarios[0].hora) + ":" + String(horarios[0].minuto < 10 ? "0" : "") + String(horarios[0].minuto);
    } else {
      return "Nenhum horario";
    }
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(onboardLedPin, OUTPUT);
  digitalWrite(onboardLedPin, LOW);

  pinMode(buzzerPin, OUTPUT);
  digitalWrite(buzzerPin, LOW);

  Wire.begin(OLED_SDA, OLED_SCL);
  u8g2.begin();
  u8g2.setFont(u8g2_font_ncenB08_tr);

  displayUmaLinha("SEGURSAT TECH");
  delay(1000);

  displayDuasLinhas("Conectando a", String(ssid));

  WiFi.begin(ssid, password);

  const unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000;
  unsigned long startTime = millis();

  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - startTime >= WIFI_CONNECT_TIMEOUT_MS) {
      displayUmaLinha("Erro no Wi-Fi!");
      delay(3000);
      break;
    }
    piscarPino(onboardLedPin, 50, 450, 1);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    displayDuasLinhas("WiFi Conectado!", "IP: " + WiFi.localIP().toString());
    piscarPino(onboardLedPin, pulsoWifiConectadoDuracao, 0, 1);

    timeClient.begin();
    timeClient.setTimeOffset(gmtOffset_sec);

    displayUmaLinha("Sincronizando NTP");
    delay(1000);
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
      int diaDaSemana = timeClient.getDay();  // 0=Dom, 1=Seg, ..., 6=Sab

      // Exibe hora atual e próximo horário no OLED
      String horaStr = String(horaAtual < 10 ? "0" : "") + String(horaAtual) + ":" + String(minutoAtual < 10 ? "0" : "") + String(minutoAtual) + ":" + String(segundoAtual < 10 ? "0" : "") + String(segundoAtual);
      String proximoHorarioStr = getProximoHorario(horaAtual, minutoAtual);

      String diaStr;
      switch (diaDaSemana) {
        case 0: diaStr = "Domingo"; break;
        case 1: diaStr = "Segunda"; break;
        case 2: diaStr = "Terca"; break;
        case 3: diaStr = "Quarta"; break;
        case 4: diaStr = "Quinta"; break;
        case 5: diaStr = "Sexta"; break;
        case 6: diaStr = "Sabado"; break;
        default: diaStr = "???"; break;
      }

      // Atualiza o display com 3 linhas
      u8g2.clearBuffer();
      u8g2.drawStr(0, 12, ("Hora: " + horaStr).c_str());
      u8g2.drawStr(0, 28, proximoHorarioStr.c_str());
      u8g2.drawStr(0, 44, ("Dia: " + diaStr).c_str());
      u8g2.sendBuffer();

      // Reset de acionamento à meia-noite
      if (horaAtual == 0 && minutoAtual == 0 && segundoAtual < 2) {
        if (!horarios[0].acionadoHoje) {  // Evita resetar múltiplas vezes
          Serial.println("Resetando agendamentos para um novo dia.");
          for (int i = 0; i < numHorarios; i++) {
            horarios[i].acionadoHoje = false;
          }
        }
      }

      // Lógica de acionamento do alarme em dias de semana
      if (diaDaSemana >= 1 && diaDaSemana <= 5) {
        for (int i = 0; i < numHorarios; i++) {
          if (horaAtual == horarios[i].hora && minutoAtual == horarios[i].minuto && !horarios[i].acionadoHoje) {
            horarios[i].acionadoHoje = true;  // Marca como acionado ANTES para evitar repetição

            Serial.print("Acionando sirene as ");
            Serial.println(String(horarios[i].hora) + ":" + String(horarios[i].minuto));
            displayDuasLinhas("Alarme:", String(horarios[i].hora) + ":" + String(horarios[i].minuto));

            digitalWrite(buzzerPin, HIGH);
            delay(pulsoAgendadoDuracao);
            digitalWrite(buzzerPin, LOW);

            displayUmaLinha("Pulso Concluido!");
            delay(1000);  // Pequeno delay para a mensagem ser lida
            break;
          }
        }
      }
    }
  } else {
    // --- LÓGICA DE RECONEXÃO NÃO-BLOQUEANTE
    unsigned long tempoAtualMillis = millis();
    if (tempoAtualMillis - ultimaTentativaWifi >= intervaloReconexaoWifi) {
      ultimaTentativaWifi = tempoAtualMillis;  // Atualiza o tempo da última tentativa

      displayUmaLinha("Reconectando...");
      piscarPino(onboardLedPin, 50, 50, 3);  // Pisca rápido para indicar tentativa

      WiFi.reconnect();
    }
  }
}