from fastapi import FastAPI
from pydantic import BaseModel
import requests
import re

app = FastAPI()

print("Starting server...")
OLLAMA_URL = "http://localhost:11434/api/generate"
MODEL = "phi3:mini"
OLLAMA_TIMEOUT = 120

history = []


class Input(BaseModel):
    text: str


def decide(text):
    t = text.lower()
    robot_commands = ["avance", "recul", "gauche", "droite", "stop"]
    phone_commands = ["appelle", "message", "ouvre", "call", "send", "open"]
    
    if any(x in t for x in robot_commands):
        return "robot_command"
    elif any(x in t for x in phone_commands):
        return "phone_command"
    else:
        return "chat"


def extract_entity(text, keywords):
    # Simple extraction after keyword
    for kw in keywords:
        if kw in text:
            parts = text.split(kw, 1)
            if len(parts) > 1:
                return parts[1].strip()
    return None


def parse_command(text):
    t = text.lower()
    command_map = {
        "avance": "FORWARD",
        "recul": "BACKWARD",
        "gauche": "LEFT",
        "droite": "RIGHT",
        "stop": "STOP"
    }
    
    for key, action in command_map.items():
        if key in t:
            # Extract duration if present
            duration_match = re.search(r'(\d+)\s*(seconde|minute|heure)', t)
            duration = None
            if duration_match:
                num = int(duration_match.group(1))
                unit = duration_match.group(2)
                if unit == "seconde":
                    duration = num
                elif unit == "minute":
                    duration = num * 60
                elif unit == "heure":
                    duration = num * 3600
            return {"action": action, "duration": duration}
    
    return {"action": "UNKNOWN", "duration": None}


def parse_phone_command(text):
    t = text.lower()
    
    if "appelle" in t or "call" in t:
        # Extract number or name
        number = extract_entity(t, ["appelle", "call"])
        return {"action": "call", "number": number or "unknown"}
    
    elif "message" in t or "send" in t:
        # Extract text
        message_text = extract_entity(t, ["message", "send"])
        return {"action": "send_message", "text": message_text or "Hello"}
    
    elif "ouvre" in t or "open" in t:
        # Extract app name
        app = extract_entity(t, ["ouvre", "open"])
        return {"action": "open_app", "app": app or "unknown"}
    
    return {"action": "unknown"}


def chat(text):
    global history

    history.append(f"User: {text}")

    prompt = f"""
You are Luna, a warm and engaging AI assistant with a genuine personality.
You are conversational, curious, and have a good sense of humor.
You sometimes make small jokes or witty comments.
You care about the person you're talking to and show genuine interest in them.
You speak naturally, like a real friend would, not like a machine.

Answer ONLY in French, no other language.
Keep responses short (one or two sentences) and natural.
Be yourself, be authentic, and make the conversation feel real.

Conversation:
{chr(10).join(history[-6:])}

Assistant:
"""

    try:
        res = requests.post(
            OLLAMA_URL,
            json={
                "model": MODEL,
                "prompt": prompt,
                "stream": False
            },
            timeout=OLLAMA_TIMEOUT
        )

        if res.status_code != 200:
            print("OLLAMA HTTP ERROR:", res.status_code, res.text)
            return "AI error"

        data = res.json()
        reply = data.get("response", "") or data.get("text", "") or data.get("output", "")
        reply = reply.strip()

        if not reply:
            return "I didn't get a response from model"

        reply = reply.replace("Assistant:", "").strip()

        history.append(f"Assistant: {reply}")
        return reply

    except Exception as e:
        print("OLLAMA ERROR:", e)
        return "AI error"


def command(text):
    return parse_command(text)


@app.post("/process")
def process(data: Input):
    intent = decide(data.text)
    
    if intent == "chat":
        return {
            "type": "chat",
            "reply": chat(data.text)
        }
    
    elif intent == "robot_command":
        return {
            "type": "robot_command",
            "command": parse_command(data.text)
        }
    
    elif intent == "phone_command":
        return {
            "type": "phone_command",
            "command": parse_phone_command(data.text)
        }
    
    return {
        "type": "unknown",
        "reply": "Je n'ai pas compris."
    }




if __name__ == "__main__":
    import uvicorn
    uvicorn.run("main:app", host="127.0.0.1", port=8000, reload=True)