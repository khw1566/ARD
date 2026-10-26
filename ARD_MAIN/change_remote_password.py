"""Interactive local password rotation; never accepts passwords on the command line."""
import getpass
import json
from pathlib import Path
import secrets
from werkzeug.security import generate_password_hash


if __name__ == "__main__":
    local = Path(__file__).resolve().parent / ".remote-access"
    config_file = local / "gateway.json"
    config = json.loads(config_file.read_text(encoding="utf-8"))
    password = getpass.getpass("New password (12+ characters): ")
    if len(password) < 12:
        raise SystemExit("Use at least 12 characters.")
    if password != getpass.getpass("Confirm password: "):
        raise SystemExit("Passwords do not match.")
    config["password_hash"] = generate_password_hash(password)
    config["session_secret"] = secrets.token_urlsafe(48)
    temporary = config_file.with_suffix(".new")
    temporary.write_text(json.dumps(config, indent=2), encoding="utf-8")
    temporary.replace(config_file)
    (local / "connection.txt").write_text(
        f"ARD Control Center\nURL: https://{config['hostname']}\nPassword: {password}\n", encoding="utf-8")
    print("Password updated. Restart the gateway to apply it.")
