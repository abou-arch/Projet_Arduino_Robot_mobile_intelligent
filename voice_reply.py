import pyttsx3

engine = pyttsx3.init()
engine.setProperty('rate', 180)

engine.say("Bonjour, je suis ton robot")
engine.runAndWait()