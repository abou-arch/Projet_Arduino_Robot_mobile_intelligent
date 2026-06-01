/* =========================================================================
 *  PROJET : ROBOT MOBILE INTELLIGENT
 *  PHASES 1 + 2 : Deplacements simples + Page web embarquee
 *  Carte cible : ESP32 (module ESP-WROOM-32, DOIT DevKit V1, NodeMCU-ESP32, etc.)
 *  Driver moteur : L298N (IN1..IN4 + ENA/ENB)
 *  Auteur : Abou Camara
 *
 *  Pilotage possible :
 *    1. Moniteur serie (115200 bauds) :
 *         CMD|ACTION|DUREE_MS              -> vitesse par defaut
 *         CMD|ACTION|DUREE_MS|VITESSE      -> vitesse precisee (0-255)
 *
 *    2. Page web embarquee (Wi-Fi) :
 *         GET /             -> controle_robot.html
 *         GET /style.css    -> style.css
 *         GET /script.js    -> script.js
 *         GET /cmd?action=FWD&duration=1000&speed=200
 *         GET /ping
 * ========================================================================= */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

// ----- Parametres Wi-Fi -----
const char* WIFI_SSID     = "VOTRE_SSID";
const char* WIFI_PASSWORD = "VOTRE_PASSWORD";

// ----- Broches ESP32 / L298N -----
const int LEFT_MOTOR_IN1  = 26;
const int LEFT_MOTOR_IN2  = 27;
const int LEFT_MOTOR_ENA  = 25;   // PWM
const int RIGHT_MOTOR_IN1 = 18;
const int RIGHT_MOTOR_IN2 = 19;
const int RIGHT_MOTOR_ENB = 5;    // PWM
const int STATUS_LED      = 2;    // LED interne sur la plupart des cartes

// ----- PWM (API ledc specifique ESP32) -----
const int PWM_FREQ          = 1000;
const int PWM_RESOLUTION    = 8;     // 0 a 255
const int PWM_CHANNEL_LEFT  = 0;
const int PWM_CHANNEL_RIGHT = 1;

// ----- Securite -----
const unsigned long MAX_DURATION_MS = 2000;
const int           DEFAULT_SPEED   = 200;
const int           MAX_SPEED       = 255;
const int           MIN_SPEED_MOVE  = 80;

enum class RobotAction { FORWARD, BACKWARD, LEFT, RIGHT, STOP, INVALID };

struct RobotCommand {
  String        prefix;
  RobotAction   action;
  unsigned long duration;
  int           speed;
};

// =========================================================================
// CLASSE 1 : MotorController
// =========================================================================
class MotorController {
public:
  void setupPins() {
    pinMode(LEFT_MOTOR_IN1,  OUTPUT);
    pinMode(LEFT_MOTOR_IN2,  OUTPUT);
    pinMode(RIGHT_MOTOR_IN1, OUTPUT);
    pinMode(RIGHT_MOTOR_IN2, OUTPUT);
    pinMode(STATUS_LED,      OUTPUT);

    // Canaux PWM
    ledcSetup(PWM_CHANNEL_LEFT,  PWM_FREQ, PWM_RESOLUTION);
    ledcSetup(PWM_CHANNEL_RIGHT, PWM_FREQ, PWM_RESOLUTION);
    ledcAttachPin(LEFT_MOTOR_ENA,  PWM_CHANNEL_LEFT);
    ledcAttachPin(RIGHT_MOTOR_ENB, PWM_CHANNEL_RIGHT);

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
    ledcWrite(PWM_CHANNEL_LEFT,  0);
    ledcWrite(PWM_CHANNEL_RIGHT, 0);
    digitalWrite(STATUS_LED, LOW);
  }

private:
  void applySpeed(int speed) {
    int safeSpeed = constrain(speed, 0, MAX_SPEED);
    ledcWrite(PWM_CHANNEL_LEFT,  safeSpeed);
    ledcWrite(PWM_CHANNEL_RIGHT, safeSpeed);
    digitalWrite(STATUS_LED, safeSpeed > 0 ? HIGH : LOW);
  }
};

// =========================================================================
// CLASSE 2 : SafetyManager
// =========================================================================
class SafetyManager {
public:
  unsigned long validateDuration(unsigned long duration) {
    return duration > MAX_DURATION_MS ? MAX_DURATION_MS : duration;
  }
  int validateSpeed(int speed) {
    if (speed <= 0)             return 0;
    if (speed > MAX_SPEED)      return MAX_SPEED;
    if (speed < MIN_SPEED_MOVE) return MIN_SPEED_MOVE;
    return speed;
  }
};

// =========================================================================
// CLASSE 3 : CommandParser
// =========================================================================
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
    if (cmd.prefix != "CMD")                return false;
    if (cmd.action == RobotAction::INVALID) return false;
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

// =========================================================================
// CLASSE 4 : RobotController
// =========================================================================
class RobotController {
public:
  RobotController() : m_isMoving(false), m_startTime(0), m_currentDuration(0),
                      m_lastReply("Robot pret.") {}

  void setup() {
    m_motorController.setupPins();
    Serial.begin(115200);
    Serial.println();
    Serial.println("Robot pret (ESP32 + PWM + Web).");
    Serial.println("Format : CMD|ACTION|DUREE_MS  ou  CMD|ACTION|DUREE_MS|VITESSE");
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
    m_isMoving = true;
    m_startTime = millis();
    m_currentDuration = duration;
  }
  void checkTimeout() {
    if (m_isMoving && (millis() - m_startTime >= m_currentDuration)) emergencyStop();
  }
  void emergencyStop() {
    m_motorController.stop();
    m_isMoving = false;
    m_startTime = 0;
    m_currentDuration = 0;
  }
};

// =========================================================================
// RESSOURCES WEB EMBARQUEES
// =========================================================================
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="fr"><head><meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Controle du Robot</title><link rel="stylesheet" href="/style.css"></head>
<body><header><h1>Controle du Robot</h1>
<div id="statusIndicator" class="status"><span class="dot"></span><span id="statusText">En attente...</span></div>
</header><main class="card"><div class="pad">
<button class="btn fwd" data-action="FWD">^</button>
<button class="btn left" data-action="LEFT"><</button>
<button class="btn stop" data-action="STOP">STOP</button>
<button class="btn right" data-action="RIGHT">></button>
<button class="btn bwd" data-action="BWD">v</button></div>
<div class="field"><label>Vitesse <span><span id="speedValue">200</span> / 255</span></label>
<input type="range" id="speedSlider" min="80" max="255" value="200"></div>
<div class="field"><label>Duree <span><span id="durationValue">1000</span> ms</span></label>
<input type="range" id="durationSlider" min="100" max="2000" step="100" value="1000"></div>
<div class="log" id="log"><div class="line">Console prete.</div></div>
</main><script src="/script.js"></script></body></html>
)rawliteral";

const char STYLE_CSS[] PROGMEM = R"rawliteral(
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:Arial,sans-serif;background:linear-gradient(160deg,#0f172a,#1e293b);
 color:#f1f5f9;min-height:100vh;display:flex;flex-direction:column;align-items:center;padding:20px}
header{text-align:center;margin-bottom:25px}
h1{font-size:1.8rem;color:#38bdf8;margin-bottom:8px}
.status{display:inline-flex;align-items:center;gap:8px;font-size:.9rem;color:#94a3b8}
.status .dot{width:10px;height:10px;border-radius:50%;background:#ef4444;box-shadow:0 0 8px #ef4444}
.status.online .dot{background:#22c55e;box-shadow:0 0 8px #22c55e}
.card{background:rgba(15,23,42,.6);border:1px solid rgba(56,189,248,.25);
 border-radius:16px;padding:24px;width:100%;max-width:420px}
.pad{display:grid;grid-template-columns:repeat(3,1fr);grid-template-rows:repeat(3,1fr);
 gap:10px;aspect-ratio:1/1;margin:10px auto 20px}
.btn{border:none;border-radius:14px;font-size:1.3rem;font-weight:600;color:#f8fafc;
 background:linear-gradient(180deg,#1e40af,#1e3a8a);cursor:pointer;box-shadow:0 4px 0 #0c1c5c}
.btn:active{transform:translateY(2px);box-shadow:0 2px 0 #0c1c5c}
.btn.stop{background:linear-gradient(180deg,#dc2626,#991b1b);box-shadow:0 4px 0 #5b0e0e}
.btn.fwd{grid-column:2;grid-row:1}.btn.left{grid-column:1;grid-row:2}
.btn.stop{grid-column:2;grid-row:2}.btn.right{grid-column:3;grid-row:2}.btn.bwd{grid-column:2;grid-row:3}
.field{margin-top:18px}
.field label{display:flex;justify-content:space-between;font-size:.95rem;margin-bottom:8px;color:#cbd5e1}
.field label span{color:#38bdf8;font-weight:600}
input[type=range]{width:100%;accent-color:#38bdf8}
.log{margin-top:22px;background:#020617;border:1px solid rgba(56,189,248,.2);
 border-radius:10px;padding:12px;font-family:monospace;font-size:.85rem;
 color:#94a3b8;height:120px;overflow-y:auto}
.log .ok{color:#4ade80}.log .err{color:#f87171}
)rawliteral";

const char SCRIPT_JS[] PROGMEM = R"rawliteral(
const speedSlider=document.getElementById('speedSlider');
const durationSlider=document.getElementById('durationSlider');
const speedValue=document.getElementById('speedValue');
const durationValue=document.getElementById('durationValue');
const logBox=document.getElementById('log');
const statusIndicator=document.getElementById('statusIndicator');
const statusText=document.getElementById('statusText');
speedSlider.addEventListener('input',()=>speedValue.textContent=speedSlider.value);
durationSlider.addEventListener('input',()=>durationValue.textContent=durationSlider.value);
function logLine(t,type=''){const l=document.createElement('div');l.className='line '+type;
 l.textContent='['+new Date().toLocaleTimeString()+'] '+t;logBox.appendChild(l);
 logBox.scrollTop=logBox.scrollHeight;
 while(logBox.children.length>50) logBox.removeChild(logBox.firstChild);}
function setOnline(o){if(o){statusIndicator.classList.add('online');statusText.textContent='Connecte';}
 else{statusIndicator.classList.remove('online');statusText.textContent='Hors ligne';}}
async function sendCommand(action){
 const speed=speedSlider.value;
 const duration=action==='STOP'?0:durationSlider.value;
 const url='/cmd?action='+action+'&duration='+duration+'&speed='+speed;
 logLine('-> '+action+' (duree '+duration+' ms, vitesse '+speed+')');
 try{const res=await fetch(url,{cache:'no-store'});
  if(!res.ok) throw new Error('HTTP '+res.status);
  const txt=await res.text();logLine('<- '+txt,'ok');setOnline(true);}
 catch(e){logLine('Erreur : '+e.message,'err');setOnline(false);}}
document.querySelectorAll('.btn').forEach(b=>
 b.addEventListener('click',()=>sendCommand(b.dataset.action)));
document.addEventListener('keydown',e=>{if(e.repeat) return;
 switch(e.key){case 'ArrowUp':sendCommand('FWD');break;case 'ArrowDown':sendCommand('BWD');break;
 case 'ArrowLeft':sendCommand('LEFT');break;case 'ArrowRight':sendCommand('RIGHT');break;
 case ' ':sendCommand('STOP');break;}});
setInterval(async()=>{try{const r=await fetch('/ping',{cache:'no-store'});setOnline(r.ok);}
 catch{setOnline(false);}},5000);
)rawliteral";

// =========================================================================
// CLASSE 5 : WebInterface
// =========================================================================
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
  WebServer        m_server;
  RobotController& m_robot;

  void connectWifi() {
    Serial.print("Connexion au Wi-Fi : ");
    Serial.println(WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 40) {
      delay(500); Serial.print("."); attempts++;
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("Connecte ! Adresse IP : ");
      Serial.println(WiFi.localIP());
    } else {
      Serial.println("Echec Wi-Fi - verifier SSID / mot de passe.");
    }
  }

  void setupRoutes() {
    m_server.on("/", HTTP_GET, [this]() {
      addCors(); m_server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
    });
    m_server.on("/style.css", HTTP_GET, [this]() {
      addCors(); m_server.send_P(200, "text/css; charset=utf-8", STYLE_CSS);
    });
    m_server.on("/script.js", HTTP_GET, [this]() {
      addCors(); m_server.send_P(200, "application/javascript; charset=utf-8", SCRIPT_JS);
    });
    m_server.on("/cmd", HTTP_GET, [this]() { handleCmd(); });
    m_server.on("/ping", HTTP_GET, [this]() {
      addCors(); m_server.send(200, "text/plain", "OK");
    });
    m_server.onNotFound([this]() {
      addCors(); m_server.send(404, "text/plain", "Not found");
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

// =========================================================================
RobotController robot;
WebInterface    web(robot);

void setup() {
  robot.setup();
  web.begin();
}

void loop() {
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    robot.handleCommand(command);
  }
  web.update();
  robot.update();
}