import os

import nbtlib
from nbtlib import Compound, Byte, Short, List

FILE = os.path.join(os.path.dirname(__file__), "level.dat")

PLAYER_PATHS = {
    1: ("Data", "Player"),
    2: ("Data", "Player2"),
    3: ("Data", "Player3"),
    4: ("Data", "Player4"),
}

nbt = nbtlib.load(FILE)


def get_player(player_index, create=False):
    if player_index not in PLAYER_PATHS:
        return None

    node = nbt
    path = PLAYER_PATHS[player_index]

    for key in path[:-1]:
        if key not in node:
            if not create:
                return None
            node[key] = Compound()
        node = node[key]

    final_key = path[-1]
    if final_key not in node:
        if not create:
            return None
        node[final_key] = Compound({
            "Inventory": List[Compound]([]),
        })

    player = node[final_key]
    if "Inventory" not in player:
        if not create:
            return None
        player["Inventory"] = List[Compound]([])

    return player


def get_inventory(player_index, create=False):
    player = get_player(player_index, create=create)
    if player is None:
        return None
    return player["Inventory"]


def show_inventory(player_index, inventory):
    print(f"Spieler {player_index}:")
    if len(inventory) == 0:
        print("  Inventar leer.")
        return

    for item in inventory:
        print(
            f"  Slot {int(item['Slot'])}: "
            f"id={int(item['id'])}, "
            f"count={int(item['Count'])}, "
            f"damage={int(item['Damage'])}"
        )


def read_all_players():
    found = False
    for player_index in sorted(PLAYER_PATHS):
        inventory = get_inventory(player_index, create=False)
        if inventory is None:
            continue
        show_inventory(player_index, inventory)
        found = True

    if not found:
        print("Keine Spieler-Inventare gefunden.")


def write_item():
    try:
        player_index = int(input("Spieler (1-4): "))
        slot = int(input("Slot: "))
        item_id = int(input("Item-ID: "))
        count = int(input("Anzahl: "))
        damage = int(input("Damage (meist 0): "))
    except ValueError:
        print("Ungültige Eingabe.")
        return

    if player_index not in PLAYER_PATHS:
        print("Ungültiger Spieler.")
        return

    inventory = get_inventory(player_index, create=True)

    # prüfen ob Slot existiert
    for item in inventory:
        if int(item["Slot"]) == slot:
            item["id"] = Short(item_id)
            item["Count"] = Byte(count)
            item["Damage"] = Short(damage)
            print(f"Spieler {player_index}: Slot überschrieben.")
            return

    # sonst neu anlegen
    inventory.append(Compound({
        "Slot": Byte(slot),
        "id": Short(item_id),
        "Count": Byte(count),
        "Damage": Short(damage),
    }))
    print(f"Spieler {player_index}: Neues Item hinzugefügt.")

def help_text():
    print("""
Befehle:
 read   - Inventare aller Spieler anzeigen
 write  - Item in Slot eines Spielers setzen
 save   - level.dat speichern
 help   - diese Hilfe
 exit   - Programm beenden
""")

help_text()

while True:
    cmd = input("> ").strip().lower()

    if cmd == "read":
        read_all_players()
    elif cmd == "write":
        write_item()
    elif cmd == "save":
        nbt.save(FILE)
        print("Gespeichert.")
    elif cmd == "help":
        help_text()
    elif cmd == "exit":
        break
    else:
        print("Unbekannter Befehl.")
