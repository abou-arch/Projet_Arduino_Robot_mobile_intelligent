/* =========================================================================
 *  PROJET : ROBOT MOBILE INTELLIGENT
 *  PHASES 1 + 2 : Déplacements simples + Page web embarquée
 *  Carte cible : ESP8266 (NodeMCU, Wemos D1 mini, ou module ESP-12)
 *  Driver moteur : L298N, L9110S ou équivalent (IN1..IN4 + ENA/ENB)
 *  Auteur : Abou Camara
 *
 *  Pilotage possible :
 *
 *    1. Moniteur série (115200 bauds) :
 *         CMD|ACTION|DUREE_MS              -> vitesse par défaut
 *         CMD|ACTION|DUREE_MS|VITESSE      -> vitesse précisée (0-255)
 *
 *    2. Page web embarquée (Wi-Fi) :
 *         GET /             -> controle_robot.html
 *         GET /style.css    -> style.css
 *         GET /script.js    -> script.js
 *         GET /cmd?action=FWD&duration=1000&speed=200
 *         GET /ping
 *
 *  Différences par rapport à l'ESP32 :
 *    - Bibliothèques  : ESP8266WiFi.h + ESP8266WebServer.h
 *    - PWM            : analogWrite() (au lieu de ledcSetup/ledcWrite)
 *    - Broches        : D1..D7 (la correspondance GPIO est dans le code)
 *    - LED interne    : D4 (GPIO 2) avec logique INVERSEE (LOW = allumée)
 * ========================================================================= */

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

/* -------------------------------------------------------------------------
 *  PARAMETRES WI-FI - A PERSONNALISER
 * ------------------------------------------------------------------------- */
const char* WIFI_SSID     = "VOTRE_SSID";
const char* WIFI_PASSWORD = "VOTRE_PASSWORD";

/* -------------------------------------------------------------------------
 *  CONFIGURATION DES BROCHES (ESP8266 NodeMCU)
 *
 *  Sur l'ESP8266, on peut utiliser soit les noms "D1, D2..." (NodeMCU)
 *  soit les numéros GPIO bruts (5, 4, 0...). Les deux sont équivalents.
 *  On garde ici les macros D1..D7 pour rester proche du marquage du PCB.
 * ------------------------------------------------------------------------- */

// Moteur gauche
const int LEFT_MOTOR_IN1  = D1;   // GPIO 5  - sens
const int LEFT_MOTOR_IN2  = D2;   // GPIO 4  - sens
const int LEFT_MOTOR_ENA  = D3;   // GPIO 0  - vitesse (PWM)

// Moteur droit
const int RIGHT_MOTOR_IN1 = D5;   // GPIO 14 - sens
const int RIGHT_MOTOR_IN2 = D6;   // GPIO 12 - sens
const int RIGHT_MOTOR_ENB = D7;   // GPIO 13 - vitesse (PWM)

// LED d'état (LED bleue de la carte NodeMCU, sur D4 / GPIO 2)
// ATTENTION : sur la plupart des cartes ESP8266, cette LED a une
// logique INVERSEE -> LOW = allumée, HIGH = éteinte.
const int STATUS_LED        = D4;
const bool LED_ACTIVE_LOW   = true;   // mettre à false pour une LED externe normale

/* -------------------------------------------------------------------------
 *  PARAMETRES PWM (analogWrite sur ESP8266)
 *
 *  Par défaut, analogWrite() attend des valeurs 0..1023. On le règle ici
 *  sur 0..255 pour rester compatible avec la même logique que l'ESP32.
 * ------------------------------------------------------------------------- */
const int PWM_FREQ       = 1000;   // 1 kHz
const int PWM_RANGE_MAX  = 255;    // valeurs 0..255

/* -------------------------------------------------------------------------
 *  CONSTANTES DE SECURITE / VALEURS PAR DEFAUT
 * ------------------------------------------------------------------------- */
const unsigned long MAX_DURATION_MS = 2000;
const int           DEFAULT_SPEED   = 200;
const int           MAX_SPEED       = 255;
const int           MIN_SPEED_MOVE  = 80;

/* -------------------------------------------------------------------------
 *  ENUM ET STRUCTURE DE COMMANDE
 * ------------------------------------------------------------------------- */
enum class RobotAction {
  FORWARD, BACKWARD, LEFT, RIGHT, STOP, INVALID
};

struct RobotCommand {
  String        prefix;
  RobotAction   action;
  unsigned long duration;
  int           speed;
};

/* -------------------------------------------------------------------------
 *  PETITE FONCTION pour allumer / éteindre la LED en tenant compte du
 *  cas où elle est inversée.
 * ------------------------------------------------------------------------- */
inline void writeLed(bool on) {
  if (LED_ACTIVE_LOW) digitalWrite(STATUS_LED, on ? LOW  : HIGH);
  else                digitalWrite(STATUS_LED, on ? HIGH : LOW);
}

/* =========================================================================
 *  CLASSE 1 : MotorController
 *  Pilote sens (IN1..IN4) + vitesse (analogWrite sur ENA/ENB).
 * ========================================================================= */
class MotorController {
public:
  void setupPins() {
    pinMode(LEFT_MOTOR_IN1,  OUTPUT);
    pinMode(LEFT_MOTOR_IN2,  OUTPUT);
    pinMode(LEFT_MOTOR_ENA,  OUTPUT);
    pinMode(RIGHT_MOTOR_IN1, OUTPUT);
    pinMode(RIGHT_MOTOR_IN2, OUTPUT);
    pinMode(RIGHT_MOTOR_ENB, OUTPUT);
    pinMode(STATUS_LED,      OUTPUT);

    // Réglage du PWM ESP8266 (0..255 à 1 kHz)
    analogWriteRange(PWM_RANGE_MAX);
    analogWriteFreq(PWM_FREQ);

    stop();
  }

  void moveForward(int speed) {
    digitalWrite(LEFT_MOTOR_IN1,  HIGH);
    digitalWrite(LEFT_MOTOR_IN2,  LOW);
    digitalWrite(RIGHT_MOTOR_IN1, HIGH);
    digitalWrite(RIGHT_MOTOR_IN2, LOW);
    applySpeed(speed);
  }

  void moveBackward(int speed) {
    digitalWrite(LEFT_MOTOR_IN1,  LOW);
    digitalWrite(LEFT_MOTOR_IN2,  HIGH);
    digitalWrite(RIGHT_MOTOR_IN1, LOW);
    digitalWrite(RIGHT_MOTOR_IN2, HIGH);
    applySpeed(speed);
  }

  void turnLeft(int speed) {
    digitalWrite(LEFT_MOTOR_IN1,  LOW);
    digitalWrite(LEFT_MOTOR_IN2,  HIGH);
    digitalWrite(RIGHT_MOTOR_IN1, HIGH);
    digitalWrite(RIGHT_MOTOR_IN2, LOW);
    applySpeed(speed);
  }

  void turnRight(int speed) {
    digitalWrite(LEFT_MOTOR_IN1,  HIGH);
    digitalWrite(LEFT_MOTOR_IN2,  LOW);
    digitalWrite(RIGHT_MOTOR_IN1, LOW);
    digitalWrite(RIGHT_MOTOR_IN2, HIGH);
    applySpeed(speed);
  }

  void stop() {
    digitalWrite(LEFT_MOTOR_IN1,  LOW);
    digitalWrite(LEFT_MOTOR_IN2,  LOW);
    digitalWrite(RIGHT_MOTOR_IN1, LOW);
    digitalWrite(RIGHT_MOTOR_IN2, LOW);
    analogWrite(LEFT_MOTOR_ENA,  0);
    analogWrite(RIGHT_MOTOR_ENB, 0);
    writeLed(false);
  }

private:
  void applySpeed(int speed) {
    int safeSpeed = constrain(speed, 0, MAX_SPEED);
    analogWrite(LEFT_MOTOR_ENA,  safeSpeed);
    analogWrite(RIGHT_MOTOR_ENB, safeSpeed);
    writeLed(safeSpeed > 0);
  }
};

/* =========================================================================
 *  CLASSE 2 : SafetyManager
 * ========================================================================= */
class SafetyManager {
public:
  unsigned long validateDuration(unsigned long duration) {
    if (duration > MAX_DURATION_MS) return MAX_DURATION_MS;
    return duration;
  }

  int validateSpeed(int speed) {
    if (speed <= 0)             return 0;
    if (speed > MAX_SPEED)      return MAX_SPEED;
    if (speed < MIN_SPEED_MOVE) return MIN_SPEED_MOVE;
    return speed;
  }
};

/* =========================================================================
 *  CLASSE 3 : CommandParser
 * ========================================================================= */
class CommandParser {
public:
  RobotCommand parseCommand(const String& rawCommand) {
    RobotCommand cmd;
    cmd.prefix   = "";
    cmd.action   = RobotAction::INVALID;
    cmd.duration = 0;
    cmd.speed    = DEFAULT_SPEED;

    String command = rawCommand;
    command.trim();
    command.replace(" ", "");

    int firstSep  = command.indexOf('|');
    int secondSep = command.indexOf('|', firstSep + 1);
    int thirdSep  = command.indexOf('|', secondSep + 1);

    if (firstSep == -1 || secondSep == -1) return cmd;

    String prefix    = command.substring(0, firstSep);
    String actionStr = command.substring(firstSep + 1, secondSep);
    String durationStr;
    String speedStr  = "";

    if (thirdSep == -1) {
      durationStr = command.substring(secondSep + 1);
    } else {
      durationStr = command.substring(secondSep + 1, thirdSep);
      speedStr    = command.substring(thirdSep + 1);
    }

    cmd.prefix   = prefix;
    cmd.action   = stringToAction(actionStr);
    cmd.duration = (unsigned long) durationStr.toInt();
    if (speedStr.length() > 0) cmd.speed = speedStr.toInt();

    return cmd;
  }

  bool isValidCommand(const RobotCommand& cmd) {
    if (cmd.prefix != "CMD")                  return false;
    if (cmd.action == RobotAction::INVALID)   return false;
    return true;
  }

private:
  RobotAction stringToAction(const String& actionStr) {
    if (actionStr == "FWD")   return RobotAction::FORWARD;
    if (actionStr == "BWD")   return RobotAction::BACKWARD;
    if (actionStr == "LEFT")  return RobotAction::LEFT;
    if (actionStr == "RIGHT") return RobotAction::RIGHT;
    if (actionStr == "STOP")  return RobotAction::STOP;
    return RobotAction::INVALID;
  }
};

/* =========================================================================
 *  CLASSE 4 : RobotController
 * ========================================================================= */
class RobotController {
public:
  RobotController()
    : m_isMoving(false), m_startTime(0), m_currentDuration(0),
      m_lastReply("Robot pret.") {}

  void setup() {
    m_motorController.setupPins();
    Serial.begin(115200);
    Serial.println();
    Serial.println("Robot pret (ESP8266 + PWM + Web).");
    Serial.println("Format : CMD|ACTION|DUREE_MS  ou  CMD|ACTION|DUREE_MS|VITESSE");
    Serial.print("Vitesse par defaut : ");
    Serial.println(DEFAULT_SPEED);
  }

  String handleCommand(const String& rawCommand) {
    RobotCommand cmd = m_parser.parseCommand(rawCommand);

    if (!m_parser.isValidCommand(cmd)) {
      Serial.println("Commande invalide.");
      emergencyStop();
      m_lastReply = "Commande invalide.";
      return m_lastReply;
    }

    cmd.duration = m_safetyManager.validateDuration(cmd.duration);
    cmd.speed    = m_safetyManager.validateSpeed(cmd.speed);

    return executeCommand(cmd);
  }

  void update() { checkTimeout(); }
  String lastReply() const { return m_lastReply; }

private:
  MotorController m_motorController;
  CommandParser   m_parser;
  SafetyManager   m_safetyManager;

  bool          m_isMoving;
  unsigned long m_startTime;
  unsigned long m_currentDuration;
  String        m_lastReply;

  String executeCommand(const RobotCommand& cmd) {
    String reply;
    switch (cmd.action) {
      case RobotAction::FORWARD:
        m_motorController.moveForward(cmd.speed);
        startTimedMotion(cmd.duration);
        reply = "FORWARD @ " + String(cmd.speed); break;

      case RobotAction::BACKWARD:
        m_motorController.moveBackward(cmd.speed);
        startTimedMotion(cmd.duration);
        reply = "BACKWARD @ " + String(cmd.speed); break;

      case RobotAction::LEFT:
        m_motorController.turnLeft(cmd.speed);
        startTimedMotion(cmd.duration);
        reply = "LEFT @ " + String(cmd.speed); break;

      case RobotAction::RIGHT:
        m_motorController.turnRight(cmd.speed);
        startTimedMotion(cmd.duration);
        reply = "RIGHT @ " + String(cmd.speed); break;

      case RobotAction::STOP:
        emergencyStop();
        reply = "STOP"; break;

      default:
        emergencyStop();
        reply = "INVALID"; break;
    }
    Serial.println(reply);
    m_lastReply = reply;
    return reply;
  }

  void startTimedMotion(unsigned long duration) {
    m_isMoving        = true;
    m_startTime       = millis();
    m_currentDuration = duration;
  }

  void checkTimeout() {
    if (m_isMoving && (millis() - m_startTime >= m_currentDuration)) {
      emergencyStop();
    }
  }

  void emergencyStop() {
    m_motorController.stop();
    m_isMoving        = false;
    m_startTime       = 0;
    m_currentDuration = 0;
  }
};

/* =========================================================================
 *  RESSOURCES WEB EMBARQUEES
 *  Trois fichiers stockés en PROGMEM (mémoire programme, pas RAM).
 * ========================================================================= */

// ----- HTML -----
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="fr">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Contrôle du Robot</title>
<link rel="stylesheet" href="/style.css">
</head>
<body>
<header>
<h1>Contrôle du Robot</h1>
<div id="statusIndicator" class="status">
<span class="dot"></span><span id="statusText">En attente…</span>
</div>
</header>
<main class="card">
<div class="pad">
<button class="btn fwd"   data-action="FWD">▲</button>
<button class="btn left"  data-action="LEFT">◀</button>
<button class="btn stop"  data-action="STOP">■</button>
<button class="btn right" data-action="RIGHT">▶</button>
<button class="btn bwd"   data-action="BWD">▼</button>
</div>
<div class="field">
<label>Vitesse <span><span id="speedValue">200</span> / 255</span></label>
<input type="range" id="speedSlider" min="80" max="255" value="200">
</div>
<div class="field">
<label>Durée <span><span id="durationValue">1000</span> ms</span></label>
<input type="range" id="durationSlider" min="100" max="2000" step="100" value="1000">
</div>
<div class="settings">
Adresse de l'ESP8266 : <input type="text" id="esp32Ip" value="">
</div>
<div class="log" id="log"><div class="line">Console prête. Appuyez sur un bouton…</div></div>
</main>
<footer>Projet Robot Mobile Intelligent — Phase 2</footer>
<script src="/script.js"></script>
</body>
</html>
)rawliteral";


// ----- CSS -----
const char STYLE_CSS[] PROGMEM = R"rawliteral(
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Arial,sans-serif;
 background:linear-gradient(160deg,#0f172a 0%,#1e293b 100%);color:#f1f5f9;
 min-height:100vh;display:flex;flex-direction:column;align-items:center;padding:20px}
header{text-align:center;margin-bottom:25px}
h1{font-size:1.8rem;letter-spacing:1px;color:#38bdf8;margin-bottom:8px}
.status{display:inline-flex;align-items:center;gap:8px;font-size:.9rem;color:#94a3b8}
.status .dot{width:10px;height:10px;border-radius:50%;background:#ef4444;
 box-shadow:0 0 8px #ef4444;transition:.3s}
.status.online .dot{background:#22c55e;box-shadow:0 0 8px #22c55e}
.card{background:rgba(15,23,42,.6);border:1px solid rgba(56,189,248,.25);
 border-radius:16px;padding:24px;width:100%;max-width:420px;
 backdrop-filter:blur(8px);box-shadow:0 8px 25px rgba(0,0,0,.4)}
.pad{display:grid;grid-template-columns:repeat(3,1fr);grid-template-rows:repeat(3,1fr);
 gap:10px;aspect-ratio:1/1;margin:10px auto 20px}
.btn{border:none;border-radius:14px;font-size:1.5rem;font-weight:600;color:#f8fafc;
 background:linear-gradient(180deg,#1e40af 0%,#1e3a8a 100%);cursor:pointer;
 transition:transform .1s,filter .2s,box-shadow .2s;box-shadow:0 4px 0 #0c1c5c;
 user-select:none;-webkit-tap-highlight-color:transparent}
.btn:hover{filter:brightness(1.15)}
.btn:active{transform:translateY(2px);box-shadow:0 2px 0 #0c1c5c}
.btn.stop{background:linear-gradient(180deg,#dc2626 0%,#991b1b 100%);
 box-shadow:0 4px 0 #5b0e0e}
.btn.stop:active{box-shadow:0 2px 0 #5b0e0e}
.btn.fwd{grid-column:2;grid-row:1}
.btn.left{grid-column:1;grid-row:2}
.btn.stop{grid-column:2;grid-row:2}
.btn.right{grid-column:3;grid-row:2}
.btn.bwd{grid-column:2;grid-row:3}
.field{margin-top:18px}
.field label{display:flex;justify-content:space-between;font-size:.95rem;
 margin-bottom:8px;color:#cbd5e1}
.field label span{color:#38bdf8;font-weight:600}
input[type=range]{width:100%;accent-color:#38bdf8}
.settings{margin-top:20px;font-size:.85rem;color:#94a3b8;text-align:center}
.settings input{background:#0f172a;border:1px solid #334155;color:#e2e8f0;
 border-radius:6px;padding:6px 10px;width:160px;margin-left:6px;font-size:.85rem}
.log{margin-top:22px;background:#020617;border:1px solid rgba(56,189,248,.2);
 border-radius:10px;padding:12px;font-family:Consolas,"Courier New",monospace;
 font-size:.85rem;color:#94a3b8;height:120px;overflow-y:auto}
.log .line{margin-bottom:4px}
.log .ok{color:#4ade80}
.log .err{color:#f87171}
footer{margin-top:30px;font-size:.8rem;color:#64748b}
)rawliteral";


// ----- JS -----
const char SCRIPT_JS[] PROGMEM = R"rawliteral(
const speedSlider=document.getElementById('speedSlider');
const durationSlider=document.getElementById('durationSlider');
const speedValue=document.getElementById('speedValue');
const durationValue=document.getElementById('durationValue');
const ipField=document.getElementById('esp32Ip');
const logBox=document.getElementById('log');
const statusIndicator=document.getElementById('statusIndicator');
const statusText=document.getElementById('statusText');
if(ipField && !ipField.value) ipField.value=location.host;
speedSlider.addEventListener('input',()=>speedValue.textContent=speedSlider.value);
durationSlider.addEventListener('input',()=>durationValue.textContent=durationSlider.value);
function logLine(text,type=''){
 const l=document.createElement('div');
 l.className='line '+type;
 const t=new Date().toLocaleTimeString();
 l.textContent='['+t+'] '+text;
 logBox.appendChild(l);
 logBox.scrollTop=logBox.scrollHeight;
 while(logBox.children.length>50) logBox.removeChild(logBox.firstChild);
}
function setOnline(o){
 if(o){statusIndicator.classList.add('online');statusText.textContent='Connecté';}
 else{statusIndicator.classList.remove('online');statusText.textContent='Hors ligne';}
}
async function sendCommand(action){
 const speed=speedSlider.value;
 const duration=action==='STOP'?0:durationSlider.value;
 const url='/cmd?action='+action+'&duration='+duration+'&speed='+speed;
 logLine('→ '+action+' (durée '+duration+' ms, vitesse '+speed+')');
 try{
  const res=await fetch(url,{cache:'no-store'});
  if(!res.ok) throw new Error('HTTP '+res.status);
  const txt=await res.text();
  logLine('← '+txt,'ok');
  setOnline(true);
 }catch(e){
  logLine('Erreur : '+e.message,'err');
  setOnline(false);
 }
}
document.querySelectorAll('.btn').forEach(b=>
 b.addEventListener('click',()=>sendCommand(b.dataset.action)));
document.addEventListener('keydown',e=>{
 if(e.repeat) return;
 switch(e.key){
  case 'ArrowUp':sendCommand('FWD');break;
  case 'ArrowDown':sendCommand('BWD');break;
  case 'ArrowLeft':sendCommand('LEFT');break;
  case 'ArrowRight':sendCommand('RIGHT');break;
  case ' ':sendCommand('STOP');break;
 }
});
setInterval(async()=>{
 try{const r=await fetch('/ping',{cache:'no-store'});setOnline(r.ok);}
 catch{setOnline(false);}
},5000);
)rawliteral";


/* =========================================================================
 *  CLASSE 5 : WebInterface
 * ========================================================================= */
class WebInterface {
public:
  WebInterface(RobotController& robot) : m_server(80), m_robot(robot) {}

  void begin() {
    connectWifi();
    setupRoutes();
    m_server.begin();
    Serial.println("Serveur HTTP demarre sur le port 80.");
  }

  void update() { m_server.handleClient(); }

private:
  ESP8266WebServer m_server;     // <-- spécifique ESP8266
  RobotController& m_robot;

  void connectWifi() {
    Serial.print("Connexion au Wi-Fi : ");
    Serial.println(WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 40) {
      delay(500);
      Serial.print(".");
      attempts++;
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("Connecte ! Adresse IP : ");
      Serial.println(WiFi.localIP());
    } else {
      Serial.println("Echec de connexion Wi-Fi : verifier SSID / mot de passe.");
    }
  }

  void setupRoutes() {
    // Page HTML
    m_server.on("/", HTTP_GET, [this]() {
      addCors();
      m_server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
    });

    // CSS
    m_server.on("/style.css", HTTP_GET, [this]() {
      addCors();
      m_server.send_P(200, "text/css; charset=utf-8", STYLE_CSS);
    });

    // JS
    m_server.on("/script.js", HTTP_GET, [this]() {
      addCors();
      m_server.send_P(200, "application/javascript; charset=utf-8", SCRIPT_JS);
    });

    // Pilotage
    m_server.on("/cmd", HTTP_GET, [this]() { handleCmd(); });

    // Ping
    m_server.on("/ping", HTTP_GET, [this]() {
      addCors();
      m_server.send(200, "text/plain", "OK");
    });

    // 404 par défaut
    m_server.onNotFound([this]() {
      addCors();
      m_server.send(404, "text/plain", "Not found");
    });
  }

  void handleCmd() {
    addCors();
    String action   = m_server.hasArg("action")   ? m_server.arg("action")   : "STOP";
    String duration = m_server.hasArg("duration") ? m_server.arg("duration") : "0";
    String speed    = m_server.hasArg("speed")    ? m_server.arg("speed")    : String(DEFAULT_SPEED);

    String fullCommand = "CMD|" + action + "|" + duration + "|" + speed;
    String reply = m_robot.handleCommand(fullCommand);
    m_server.send(200, "text/plain", reply);
  }

  void addCors() {
    m_server.sendHeader("Access-Control-Allow-Origin",  "*");
    m_server.sendHeader("Access-Control-Allow-Methods", "GET");
    m_server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  }
};

/* =========================================================================
 *  INSTANCES GLOBALES
 * ========================================================================= */
RobotController robot;
WebInterface    web(robot);

/* =========================================================================
 *  SETUP
 * ========================================================================= */
void setup() {
  robot.setup();
  web.begin();
}

/* =========================================================================
 *  LOOP
 * ========================================================================= */
void loop() {
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    robot.handleCommand(command);
  }
  web.update();
  robot.update();
}