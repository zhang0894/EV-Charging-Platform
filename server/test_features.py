import urllib.request
import urllib.error
import json
import time
import subprocess
import sys

BASE_URL = "http://127.0.0.1:8080"

def request(method, path, body=None, token=None):
    url = BASE_URL + path
    headers = {"Content-Type": "application/json"}
    if token:
        headers["Authorization"] = f"Bearer {token}"
    
    data = json.dumps(body).encode('utf-8') if body is not None else None
    req = urllib.request.Request(url, data=data, headers=headers, method=method)
    try:
        with urllib.request.urlopen(req) as resp:
            raw = resp.read().decode('utf-8')
            return resp.status, json.loads(raw)
    except urllib.error.HTTPError as e:
        raw = e.read().decode('utf-8')
        try:
            return e.code, json.loads(raw)
        except Exception:
            return e.code, {"raw": raw}

def print_result(name, passed, detail=""):
    status = " PASS " if passed else " FAIL "
    print(f"[{status}] {name}")
    if detail:
        print(f"       -> {detail}")
    if not passed:
        sys.exit(1)

def main():
    print("==================================================")
    print("   EV-Charging-Platform New Features Test Suite   ")
    print("==================================================")

    # ----------------------------------------------------
    # TEST 1: Phone check API (No Token needed)
    # ----------------------------------------------------
    print("\n--- TEST 1: Phone Number Verification API ---")
    
    # 1.1 Invalid phone format
    status, res = request("GET", "/api/v1/auth/check-phone?phone=12345")
    print_result("1.1 Check phone invalid format (GET)", status == 400 and res.get("code") == 10003, f"Status: {status}, Response: {res}")
    
    # 1.2 Non-existent phone via GET
    status, res = request("GET", "/api/v1/auth/check-phone?phone=19999999999")
    passed = (status == 200 and res.get("code") == 0 and res["data"]["is_registered"] is False)
    print_result("1.2 Check non-existent phone (GET)", passed, f"Response: {res}")

    # 1.3 Non-existent phone via POST
    status, res = request("POST", "/api/v1/auth/check-phone", {"phone": "19999999999"})
    passed = (status == 200 and res.get("code") == 0 and res["data"]["is_registered"] is False)
    print_result("1.3 Check non-existent phone (POST)", passed, f"Response: {res}")

    # Register a test user
    test_phone = f"13900{int(time.time()) % 100000:06d}"
    status, reg_res = request("POST", "/api/v1/auth/register", {"phone": test_phone, "password": "Password123!", "nickname": "TestUser"})
    if status != 200:
        # maybe already registered, try login
        status, log_res = request("POST", "/api/v1/auth/login-password", {"phone": test_phone, "password": "Password123!"})
        token = log_res["data"]["access_token"]
        uid = log_res["data"]["user_id"]
    else:
        token = reg_res["data"]["access_token"]
        uid = reg_res["data"]["user_id"]

    # 1.4 Registered phone check via GET
    status, res = request("GET", f"/api/v1/auth/check-phone?phone={test_phone}")
    passed = (status == 200 and res.get("code") == 0 and res["data"]["is_registered"] is True)
    print_result("1.4 Check registered phone (GET)", passed, f"Response: {res}")

    # 1.5 Registered phone check via POST
    status, res = request("POST", "/api/v1/auth/check-phone", {"phone": test_phone})
    passed = (status == 200 and res.get("code") == 0 and res["data"]["is_registered"] is True)
    print_result("1.5 Check registered phone (POST)", passed, f"Response: {res}")

    # ----------------------------------------------------
    # TEST 2: Data simulation
    # ----------------------------------------------------
    print("\n--- TEST 2: Station Data Simulation Optimization ---")
    status, st_res = request("GET", "/api/v1/stations/1")
    passed = (status == 200 and st_res.get("code") == 0)
    piles = st_res["data"]["piles"]
    idle_piles = [p for p in piles if p["status"] == "IDLE"]
    charging_piles = [p for p in piles if p["status"] == "CHARGING"]
    first_pile = next((p for p in piles if p["pile_id"].endswith("_01")), None)

    print(f"       Total piles in Station 1: {len(piles)}, IDLE: {len(idle_piles)}, CHARGING: {len(charging_piles)}")
    print_result("2.1 Pile #1 is guaranteed IDLE", first_pile is not None and first_pile["status"] == "IDLE", f"Pile #1: {first_pile}")
    print_result("2.2 Other piles have simulated CHARGING activity", len(charging_piles) > 0, f"Found {len(charging_piles)} charging piles")

    status, nearby_res = request("GET", "/api/v1/stations/nearby")
    has_simulated_occupancy = any(s["idle_piles"] < s["total_piles"] for s in nearby_res["data"]["stations"])
    sample = nearby_res["data"]["stations"][0] if nearby_res["data"]["stations"] else None
    print_result("2.3 Nearby stations show realistic pile occupancy", has_simulated_occupancy, f"Sample station {sample['station_id'] if sample else 'N/A'}: total={sample['total_piles'] if sample else 0}, idle={sample['idle_piles'] if sample else 0}")

    # ----------------------------------------------------
    # Setup test user wallet
    # ----------------------------------------------------
    request("POST", "/api/v1/wallet/recharge", {"amount": 100.0}, token)
    status, w_res = request("GET", "/api/v1/wallet/balance", token=token)
    initial_balance = w_res["data"]["balance"]
    print(f"\nUser {test_phone} (ID {uid}) wallet balance: {initial_balance} RMB")

    target_pile = "P00001_01"

    # ----------------------------------------------------
    # TEST 3: Pile Reservation
    # ----------------------------------------------------
    print("\n--- TEST 3: Pile Reservation (20 RMB Deposit) ---")
    status, res_res = request("POST", "/api/v1/charging/reserve", {"pile_id": target_pile}, token)
    passed = (status == 200 and res_res.get("code") == 0)
    print_result("3.1 Reserve pile P00001_01 succeeds", passed, f"Response: {res_res}")
    res_id = res_res["data"]["reservation_id"]
    new_bal = res_res["data"]["wallet_balance"]
    print_result("3.2 Deposit 20 RMB deducted from wallet", abs(new_bal - (initial_balance - 20.0)) < 0.01, f"New balance: {new_bal}")

    # Verify pile status in station detail is RESERVED (code 8)
    status, st_res = request("GET", "/api/v1/stations/1")
    p1 = next((p for p in st_res["data"]["piles"] if p["pile_id"] == target_pile), None)
    print_result("3.3 Pile status in station detail is RESERVED (code 8)", p1 is not None and p1["status"] == "RESERVED" and p1["status_code"] == 8 and p1["status_desc"] == "已预约锁定", f"Pile: {p1}")

    # Verify active reservation check
    status, act_res = request("GET", "/api/v1/charging/active-reservation", token=token)
    passed = (status == 200 and act_res["data"]["has_active_reservation"] is True and act_res["data"]["active_reservation"]["reservation_id"] == res_id)
    print_result("3.4 Active reservation check returns reservation details", passed, f"Active reservation: {act_res['data']['active_reservation']}")

    # ----------------------------------------------------
    # TEST 4: Collision / Conflict prevention
    # ----------------------------------------------------
    print("\n--- TEST 4: Collision Prevention (Locked Pile) ---")
    other_phone = f"13700{int(time.time()) % 100000:06d}"
    status, other_reg = request("POST", "/api/v1/auth/register", {"phone": other_phone, "password": "Password123!", "nickname": "OtherUser"})
    other_token = other_reg["data"]["access_token"]
    request("POST", "/api/v1/wallet/recharge", {"amount": 50.0}, other_token)

    # Other user attempts to reserve P00001_01
    status, conflict_res = request("POST", "/api/v1/charging/reserve", {"pile_id": target_pile}, other_token)
    print_result("4.1 Another user cannot reserve locked pile (409 PileBusyOrReserved)", status == 409 and conflict_res.get("code") == 20003, f"Status: {status}, Res: {conflict_res}")

    # Other user attempts to start charging on P00001_01
    status, conflict_start = request("POST", "/api/v1/charging/start", {"pile_id": target_pile}, other_token)
    print_result("4.2 Another user cannot start charging on locked pile (409 PileBusyOrReserved)", status == 409 and conflict_start.get("code") == 20003, f"Status: {status}, Res: {conflict_start}")

    # Same user cannot reserve a second pile while having active reservation
    status, conflict_same = request("POST", "/api/v1/charging/reserve", {"pile_id": "P00001_02"}, token)
    print_result("4.3 Same user cannot reserve multiple piles concurrently", status == 409, f"Status: {status}, Res: {conflict_same}")

    # ----------------------------------------------------
    # TEST 5: Reservation Cancellation
    # ----------------------------------------------------
    print("\n--- TEST 5: Reservation Active Cancellation (5 RMB penalty, 15 RMB refund) ---")
    status, cancel_res = request("POST", "/api/v1/charging/cancel-reservation", {"reservation_id": res_id}, token)
    passed = (status == 200 and cancel_res.get("code") == 0)
    data = cancel_res["data"]
    print_result("5.1 Cancellation succeeds", passed, f"Response: {data}")
    print_result("5.2 Financial calculation: 20 deposit - 5 fee = 15 refund", data["deposit"] == 20.0 and data["penalty_fee"] == 5.0 and data["refund_amount"] == 15.0, f"Deposit={data['deposit']}, Penalty={data['penalty_fee']}, Refund={data['refund_amount']}")

    # Verify wallet balance
    status, w_res = request("GET", "/api/v1/wallet/balance", token=token)
    expected_bal = initial_balance - 5.0
    print_result("5.3 Wallet balance accurately reflects 15 RMB refund", abs(w_res["data"]["balance"] - expected_bal) < 0.01, f"Wallet balance: {w_res['data']['balance']} (expected {expected_bal})")

    # Verify pile is back to IDLE
    status, st_res = request("GET", "/api/v1/stations/1")
    p1 = next((p for p in st_res["data"]["piles"] if p["pile_id"] == target_pile), None)
    print_result("5.4 Pile P00001_01 restored to IDLE", p1["status"] == "IDLE" and p1["status_code"] == 1, f"Pile: {p1}")

    # ----------------------------------------------------
    # TEST 6: User Arrival & Charging (Deposit Refunded in Full)
    # ----------------------------------------------------
    print("\n--- TEST 6: User Arrival & Start Charging (20 RMB Deposit Refunded) ---")
    status, res_res2 = request("POST", "/api/v1/charging/reserve", {"pile_id": target_pile}, token)
    print_result("6.1 User re-reserves P00001_01", status == 200 and res_res2.get("code") == 0)
    bal_after_res2 = res_res2["data"]["wallet_balance"]

    # Same user arrives and starts charging on P00001_01
    status, start_res = request("POST", "/api/v1/charging/start", {"pile_id": target_pile}, token)
    print_result("6.2 User arrives and starts charging on reserved pile", status == 200 and start_res.get("code") == 0, f"Order: {start_res.get('data')}")
    order_id = start_res["data"]["order_id"]

    # Check that deposit 20 RMB was refunded upon arrival
    status, w_res = request("GET", "/api/v1/wallet/balance", token=token)
    print_result("6.3 Deposit 20 RMB was refunded in full to wallet", abs(w_res["data"]["balance"] - (bal_after_res2 + 20.0)) < 0.01, f"Wallet balance: {w_res['data']['balance']}")

    # Verify pile is now CHARGING
    status, st_res = request("GET", "/api/v1/stations/1")
    p1 = next((p for p in st_res["data"]["piles"] if p["pile_id"] == target_pile), None)
    print_result("6.4 Pile P00001_01 status is now CHARGING", p1["status"] == "CHARGING" and p1["status_code"] == 3)

    # Verify active reservation is no longer active (it was fulfilled)
    status, act_res = request("GET", "/api/v1/charging/active-reservation", token=token)
    print_result("6.5 Active reservation fulfilled (no longer active)", act_res["data"]["has_active_reservation"] is False)

    # Stop and settle charging session
    status, stop_res = request("POST", "/api/v1/charging/stop", {"order_id": order_id}, token)
    status, settle_res = request("POST", "/api/v1/charging/settle", {"order_id": order_id}, token)
    print_result("6.6 Charging session stopped and settled, pile restored", settle_res.get("code") == 0)

    # ----------------------------------------------------
    # TEST 7: Timeout Automatic Release & Forfeit
    # ----------------------------------------------------
    print("\n--- TEST 7: Reservation Timeout Automatic Release (Forfeit 20 RMB) ---")
    status, res_res3 = request("POST", "/api/v1/charging/reserve", {"pile_id": target_pile}, token)
    print_result("7.1 User reserves P00001_01 for timeout test", status == 200 and res_res3.get("code") == 0)
    res_id3 = res_res3["data"]["reservation_id"]

    # Check pile is RESERVED
    status, st_res = request("GET", "/api/v1/stations/1")
    p1 = next((p for p in st_res["data"]["piles"] if p["pile_id"] == target_pile), None)
    assert p1["status"] == "RESERVED"

    # Fast forward timeout by updating expire_at in DB
    import os
    pg_env = os.environ.copy()
    pg_env["PGPASSWORD"] = "Express1."
    now_ms = int(time.time() * 1000)
    psql_cmd = f"UPDATE pile_reservations SET expire_at = {now_ms - 5000} WHERE reservation_id = '{res_id3}';"
    p = subprocess.run([
        r"C:\Program Files\PostgreSQL\18\bin\psql.exe",
        "-U", "postgres",
        "-d", "postgres",
        "-c", psql_cmd
    ], env=pg_env, capture_output=True, text=True)
    print(f"       Fast-forwarded expiration in DB: {p.stdout.strip()}")

    # Wait 2.5 seconds for ChargingSimulator's 1-second background scan loop to fire
    print("       Waiting 2.5s for simulator timeout scanner to execute...")
    time.sleep(2.5)

    # Verify pile is auto-released to IDLE
    status, st_res = request("GET", "/api/v1/stations/1")
    p1 = next((p for p in st_res["data"]["piles"] if p["pile_id"] == target_pile), None)
    print_result("7.2 Pile P00001_01 auto-released back to IDLE after timeout", p1["status"] == "IDLE", f"Pile: {p1}")

    # Verify DB reservation record is TIMEOUT with penalty 2000, refund 0
    p_chk = subprocess.run([
        r"C:\Program Files\PostgreSQL\18\bin\psql.exe",
        "-U", "postgres",
        "-d", "postgres",
        "-t", "-A",
        "-c", f"SELECT status, penalty_fee_cents, refund_amount_cents FROM pile_reservations WHERE reservation_id = '{res_id3}';"
    ], env=pg_env, capture_output=True, text=True)
    db_record = p_chk.stdout.strip()
    print_result("7.3 Reservation marked TIMEOUT with 20 RMB forfeited in DB", db_record == "TIMEOUT|2000|0", f"DB record: {db_record}")

    # Verify user active reservation is none
    status, act_res = request("GET", "/api/v1/charging/active-reservation", token=token)
    print_result("7.4 Active reservation cleared after timeout", act_res["data"]["has_active_reservation"] is False)

    print("\n==================================================")
    print("   ALL TESTS PASSED SUCCESSFULLY! (100% GREEN)    ")
    print("==================================================")

if __name__ == "__main__":
    main()
