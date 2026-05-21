"""
=============================================================================
Backend FastAPI - Projet Robot Mobile Intelligent (Phase 3)
=============================================================================

Role :
  - Recoit du texte (via POST /process) en provenance de l'assistant vocal
  - Decide s'il s'agit :
      * d'une commande robot  (avance, recule, gauche, droite, stop)
      * d'une commande telephone (appelle, message, ouvre)
      * d'une simple discussion
  - Pour la discussion, interroge le modele Ollama "phi3:mini" avec la
    persona "Luna" (definie dans CHAT_PROMPT_TEMPLATE).

Auteur : Abou Camara
=============================================================================
"""

import re
from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel
import requests


# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------
OLLAMA_URL     = "http://localhost:11434/api/generate"
MODEL          = "phi3:mini"
OLLAMA_TIMEOUT = 120
MAX_HISTORY    = 6     # nombre de messages gardes pour le contexte du chatbot


# ---------------------------------------------------------------------------
# Application FastAPI
# ---------------------------------------------------------------------------
app = FastAPI(
    title       = "Robot Mobile Intelligent",
    description = "Backend du robot - dispatch voix vers robot, telephone ou chatbot.",
    version     = "1.0",
)

# CORS - autorise les appels depuis n'importe quelle origine (page web ESP32, app mobile...)
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)

print("Starting server...")


# ---------------------------------------------------------------------------
# Modele de donnees attendues en entree
# ---------------------------------------------------------------------------
class Input(BaseModel):
    text: str


# Memoire courte du chatbot (effacee a chaque redemarrage)
history = []


# ---------------------------------------------------------------------------
# Detection de l'intention
# ---------------------------------------------------------------------------
ROBOT_KEYWORDS = ["avance", "avancer", "recul", "reculer",
                  "gauche", "droite", "stop", "arrete"]
PHONE_KEYWORDS = ["appelle", "appeler", "message", "ouvre",
                  "ouvrir", "call", "send", "open"]


def decide(text):
    """Renvoie 'robot_command', 'phone_command' ou 'chat'."""
    t = text.lower()
    if any(k in t for k in ROBOT_KEYWORDS):
        return "robot_command"
    if any(k in t for k in PHONE_KEYWORDS):
        return "phone_command"
    return "chat"


# ---------------------------------------------------------------------------
# Analyse d'une commande robot
# ---------------------------------------------------------------------------
ROBOT_MAP = {
    "avance": "FORWARD",
    "recul":  "BACKWARD",
    "gauche": "LEFT",
    "droite": "RIGHT",
    "stop":   "STOP",
    "arrete": "STOP",
}


def parse_command(text):
    """Analyse une phrase et renvoie {'action': ..., 'duration': ...}.

    La duree est exprimee en millisecondes pour etre compatible
    avec le protocole CMD|ACTION|DUREE_MS du robot ESP32.
    """
    t = text.lower()

    # Action
    action = "UNKNOWN"
    for key, value in ROBOT_MAP.items():
        if key in t:
            action = value
            break

    # Duree : "2 secondes", "1 minute", "1 heure" (avec ou sans 's' final)
    duration = None
    m = re.search(r"(\d+)\s*(seconde|minute|heure)s?", t)
    if m:
        n    = int(m.group(1))
        unit = m.group(2)
        if   unit == "seconde": duration = n * 1000
        elif unit == "minute":  duration = n * 60000
        elif unit == "heure":   duration = n * 3600000

    # Duree par defaut : 1 seconde (1000 ms) si l'utilisateur ne precise pas
    if duration is None:
        duration = 1000

    return {"action": action, "duration": duration}


# ---------------------------------------------------------------------------
# Analyse d'une commande telephone
# ---------------------------------------------------------------------------
def extract_after(text, keywords):
    """Renvoie ce qui suit le premier mot-cle trouve."""
    for kw in keywords:
        if kw in text:
            parts = text.split(kw, 1)
            if len(parts) > 1:
                return parts[1].strip(" .,;:!?")
    return None


def parse_phone_command(text):
    """Renvoie {'action': 'call|send_message|open_app|unknown', ...}."""
    t = text.lower()

    if "appelle" in t or "call" in t:
        number = extract_after(t, ["appelle", "call"])
        return {"action": "call", "number": number or "unknown"}

    if "message" in t or "send" in t:
        message_text = extract_after(t, ["message", "send"])
        return {"action": "send_message", "text": message_text or "Hello"}

    if "ouvre" in t or "open" in t:
        app_name = extract_after(t, ["ouvre", "open"])
        return {"action": "open_app", "app": app_name or "unknown"}

    return {"action": "unknown"}


# ---------------------------------------------------------------------------
# Discussion avec Ollama (persona Luna)
# ---------------------------------------------------------------------------
CHAT_PROMPT_TEMPLATE = """You are Luna, a warm and engaging AI assistant with a genuine personality.
You are conversational, curious, and have a good sense of humor.
You sometimes make small jokes or witty comments.
You care about the person you're talking to and show genuine interest in them.
You speak naturally, like a real friend would, not like a machine.

Answer ONLY in French, no other language.
Keep responses short (one or two sentences) and natural.
Be yourself, be authentic, and make the conversation feel real.

Conversation:
{history}

Assistant:"""


def call_ollama(prompt):
    """Envoie le prompt a Ollama et renvoie la reponse texte."""
    res = requests.post(
        OLLAMA_URL,
        json={"model": MODEL, "prompt": prompt, "stream": False},
        timeout=OLLAMA_TIMEOUT,
    )

    if res.status_code != 200:
        print("OLLAMA HTTP ERROR:", res.status_code, res.text)
        return "AI error"

    data  = res.json()
    reply = (data.get("response") or data.get("text") or data.get("output") or "").strip()

    if not reply:
        return "I didn't get a response from model"

    return reply.replace("Assistant:", "").strip()


def chat(text):
    """Ajoute le message a l'historique, interroge Ollama, renvoie la reponse."""
    history.append(f"User: {text}")
    recent = history[-MAX_HISTORY:]
    prompt = CHAT_PROMPT_TEMPLATE.format(history="\n".join(recent))

    try:
        reply = call_ollama(prompt)
    except requests.ConnectionError:
        print("OLLAMA ERROR: serveur non disponible")
        return "Le serveur Ollama n'est pas disponible. Verifiez qu'il est bien lance."
    except Exception as e:
        print("OLLAMA ERROR:", e)
        return f"Erreur AI : {str(e)}"

    history.append(f"Assistant: {reply}")
    return reply


# ---------------------------------------------------------------------------
# Routes HTTP
# ---------------------------------------------------------------------------
@app.get("/")
def home():
    """Page d'accueil simple pour ne plus renvoyer 404."""
    return {
        "status":    "ok",
        "service":   "Robot Mobile Intelligent backend",
        "endpoints": ["POST /process", "GET /docs", "GET /ping"],
    }


@app.get("/ping")
def ping():
    """Verification rapide que le serveur tourne."""
    return {"pong": True}


@app.post("/process")
def process(data: Input):
    """Point d'entree principal : analyse le texte recu et renvoie le bon type."""
    intent = decide(data.text)

    if intent == "chat":
        return {"type": "chat", "reply": chat(data.text)}

    if intent == "robot_command":
        return {"type": "robot_command", "command": parse_command(data.text)}

    if intent == "phone_command":
        return {"type": "phone_command", "command": parse_phone_command(data.text)}

    return {"type": "unknown", "reply": "Je n'ai pas compris."}


# ---------------------------------------------------------------------------
# Lancement direct (python main.py)
# ---------------------------------------------------------------------------
if __name__ == "__main__":
    import uvicorn
    uvicorn.run("main:app", host="127.0.0.1", port=8000, reload=True)