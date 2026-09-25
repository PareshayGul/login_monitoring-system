#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <cmath>
#include <memory>
#include <cstdlib>
#include <thread>
#include <vector>
#include "crow_all.h"
#include <windows.h>
#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")

// Some Windows SDK headers define a few plain all-caps words as macros
// for legacy reasons. If OPTIONS is one of them, it would silently mangle
// every crow::HTTPMethod::OPTIONS below. This is harmless if it was never
// defined in the first place.
#ifdef OPTIONS
#undef OPTIONS
#endif

using namespace std;

// ============================================================
// Database classes - each one knows how to APPEND a new
// signup record to its own file. Nothing else.
// ============================================================

class Database {
public:
    virtual void data(const string& name, const string& password,
                       const string& email, const string& id) = 0;
    virtual ~Database() {}
};

class DatabaseAdmin : public Database {
public:
    void data(const string& name, const string& password,
              const string& email, const string& id) override {
        ofstream file("admindata.txt", ios::app);
        if (file.is_open()) {
            // Trailing 0,0,0 = starting attempts, starting lastLat, starting lastLong
            file << name << "," << password << "," << email << ","
                 << id << ",0,0,0" << "\n";
            file.close();
        }
    }
};

class DatabaseUser : public Database {
public:
    void data(const string& name, const string& password,
              const string& email, const string& id) override {
        ofstream file("userdata.txt", ios::app);
        if (file.is_open()) {
            file << name << "," << password << "," << email << ","
                 << id << ",0,0,0" << "\n";
            file.close();
        }
    }
};

class DatabaseEmployee : public Database {
public:
    void data(const string& name, const string& password,
              const string& email, const string& id) override {
        ofstream file("employeedata.txt", ios::app);
        if (file.is_open()) {
            file << name << "," << password << "," << email << ","
                 << id << ",0,0,0" << "\n";
            file.close();
        }
    }
};

// ============================================================
// LocationChecker - compares a new latitude/longitude pair
// against a previously stored one to guess "is this a new place?"
// ============================================================

class LocationChecker {
public:
    bool isNewLocation(double lastLat, double lastLong,
                        double currentLat, double currentLong) {
        if (lastLat == 0.0 && lastLong == 0.0) {
            return false; // no previous location on record yet - nothing to compare
        }
        double latDiff = fabs(lastLat - currentLat);
        double longDiff = fabs(lastLong - currentLong);
        // Rough threshold for a student project - not a precise distance calculation.
        return (latDiff > 0.5 || longDiff > 0.5);
    }
};

// ============================================================
// Person hierarchy
// ============================================================

class Person {
protected:
    string username;
    string password;
    string email;
    string id;
    string storedPassword; // filled by lookupByUsername()
    string storedEmail; // filled by lookupByUsername()
    int attempts;
    double lastLat;
    double lastLong;

    // Every child must say which file it reads/writes and what its role is called.
    virtual string filename() const = 0;

    // Finds this username in the file (regardless of password), and if found,
    // fills storedPassword/attempts/lastLat/lastLong from that line.
    bool lookupByUsername() {
        string line;
        ifstream file(filename());
        if (!file.is_open()) return false;

        while (getline(file, line)) {
            stringstream iss(line);
            string fUsername, fPassword, fEmail, fId, fAttempts, fLat, fLong;
            getline(iss, fUsername, ',');
            getline(iss, fPassword, ',');
            getline(iss, fEmail, ',');
            getline(iss, fId, ',');
            getline(iss, fAttempts, ',');
            getline(iss, fLat, ',');
            getline(iss, fLong, ',');

            if (fUsername == username) {
                storedPassword = fPassword;
                storedEmail = fEmail;
                attempts = fAttempts.empty() ? 0 : stoi(fAttempts);
                lastLat = fLat.empty() ? 0.0 : stod(fLat);
                lastLong = fLong.empty() ? 0.0 : stod(fLong);
                file.close();
                return true;
            }
        }
        file.close();
        return false;
    }

    // Rewrites the whole file, replacing only the line matching this username
    // with an updated attempts/location, leaving every other line untouched.
    void updateRecord(int newAttempts, double newLat, double newLong) {
        ifstream inFile(filename());
        stringstream buffer;
        string line;

        while (inFile.is_open() && getline(inFile, line)) {
            stringstream iss(line);
            string fUsername, fPassword, fEmail, fId, fAttempts, fLat, fLong;
            getline(iss, fUsername, ',');
            getline(iss, fPassword, ',');
            getline(iss, fEmail, ',');
            getline(iss, fId, ',');
            getline(iss, fAttempts, ',');
            getline(iss, fLat, ',');
            getline(iss, fLong, ',');

            if (fUsername == username) {
                buffer << fUsername << "," << fPassword << "," << fEmail << ","
                       << fId << "," << newAttempts << "," << newLat << ","
                       << newLong << "\n";
            } else {
                buffer << line << "\n";
            }
        }
        inFile.close();

        ofstream outFile(filename(), ios::trunc);
        outFile << buffer.str();
        outFile.close();
    }

public:
    Person(string u, string p, string e, string i)
        : username(u), password(p), email(e), id(i),
          attempts(0), lastLat(0.0), lastLong(0.0) {}

    virtual void credentials() = 0;      // writes a new signup record
    virtual string roleName() const = 0; // "Admin" / "User" / "Employee"
    virtual ~Person() {}

    bool alreadyExists() {
        string line;
        ifstream file(filename());
        if (!file.is_open()) return false;

        while (getline(file, line)) {
            stringstream iss(line);
            string fUsername, fPassword, fEmail, fId;
            getline(iss, fUsername, ',');
            getline(iss, fPassword, ',');
            getline(iss, fEmail, ',');
            getline(iss, fId, ',');

            bool idMatches = !id.empty() && fId == id;
            if (fUsername == username || fEmail == email || idMatches) {
                file.close();
                return true;
            }
        }
        file.close();
        return false;
    }

    // Returns true on a correct login. Resets the stored attempt count to 0
    // on success, or increments it (persisted to the file) on failure.
    bool checkAttemptNumber() {
        if (!lookupByUsername()) {
            return false; // no such account in this role's file at all
        }
        if (attempts >= 6) {
            cout << "Account locked: too many failed attempts." << endl;
            return false;
        }
        if (storedPassword == password) {
            updateRecord(0, lastLat, lastLong);
            attempts = 0;
            return true;
        }
        attempts++;
        updateRecord(attempts, lastLat, lastLong);
        if (attempts >= 6) {
            cout << "Maximum attempts reached. Account locked." << endl;
        }
        return false;
    }

    bool isLocked() const { return attempts >= 6; }
    int getAttempts() const { return attempts; }
    string getEmail() const { return storedEmail; }
    double getLastLat() const { return lastLat; }
    double getLastLong() const { return lastLong; }

    void saveLoginLocation(double latitude, double longitude) {
        if (latitude != 0.0 || longitude != 0.0) {
            lastLat = latitude;
            lastLong = longitude;
        }
        updateRecord(0, lastLat, lastLong);
    }
};

class Admin : public Person {
    DatabaseAdmin db;
protected:
    string filename() const override { return "admindata.txt"; }
public:
    Admin(string u, string p, string e, string i) : Person(u, p, e, i) {}
    void credentials() override { db.data(username, password, email, id); }
    string roleName() const override { return "Admin"; }
};

class User : public Person {
    DatabaseUser db;
protected:
    string filename() const override { return "userdata.txt"; }
public:
    User(string u, string p, string e, string i) : Person(u, p, e, i) {}
    void credentials() override { db.data(username, password, email, id); }
    string roleName() const override { return "User"; }
};

class Employee : public Person {
    DatabaseEmployee db;
protected:
    string filename() const override { return "employeedata.txt"; }
public:
    Employee(string u, string p, string e, string i) : Person(u, p, e, i) {}
    void credentials() override { db.data(username, password, email, id); }
    string roleName() const override { return "Employee"; }
};

// ============================================================
// Crow HTTP routes
// ============================================================

// Adds the headers that tell the browser "requests from other origins
// (like a local file, or a different port) are allowed." Without these,
// the browser blocks the request before it ever reaches your route code.
void addCorsHeaders(crow::response& res) {
    res.add_header("Access-Control-Allow-Origin", "*");
    res.add_header("Access-Control-Allow-Methods", "POST, OPTIONS");
    res.add_header("Access-Control-Allow-Headers", "Content-Type");
}

string jsonEscape(const string& value) {
    string escaped;
    for (char character : value) {
        switch (character) {
        case '\\': escaped += "\\\\"; break;
        case '"': escaped += "\\\""; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default: escaped += character; break;
        }
    }
    return escaped;
}

wstring toWide(const string& value) {
    return wstring(value.begin(), value.end());
}

bool sendBrevoEmail(const string& recipient, const string& subject,
                    const string& htmlContent) {
    const char* apiKey = getenv("BREVO_API_KEY");
    const char* senderEmail = getenv("SENDER_EMAIL");
    if (apiKey == nullptr || senderEmail == nullptr || recipient.empty()) {
        cerr << "Email not sent: BREVO_API_KEY, SENDER_EMAIL, or recipient is missing." << endl;
        return false;
    }

    string payload = "{\"sender\":{\"email\":\"" + jsonEscape(senderEmail) +
        "\"},\"to\":[{\"email\":\"" + jsonEscape(recipient) +
        "\"}],\"subject\":\"" + jsonEscape(subject) +
        "\",\"htmlContent\":\"" + jsonEscape(htmlContent) + "\"}";

    HINTERNET session = WinHttpOpen(L"LoginApp/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return false;

    HINTERNET connection = WinHttpConnect(session, L"api.brevo.com",
        INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connection) {
        WinHttpCloseHandle(session);
        return false;
    }

    HINTERNET request = WinHttpOpenRequest(connection, L"POST", L"/v3/smtp/email",
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE);
    if (!request) {
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return false;
    }

    wstring headers = L"accept: application/json\r\nContent-Type: application/json\r\napi-key: " +
        toWide(apiKey);
    BOOL sent = WinHttpSendRequest(request, headers.c_str(), -1L,
        const_cast<char*>(payload.c_str()), static_cast<DWORD>(payload.size()),
        static_cast<DWORD>(payload.size()), 0);
    BOOL received = sent && WinHttpReceiveResponse(request, nullptr);

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return received == TRUE;
}

void queueEmail(const string& recipient, const string& subject,
                const string& htmlContent) {
    thread([recipient, subject, htmlContent]() {
        if (!sendBrevoEmail(recipient, subject, htmlContent)) {
            cerr << "Brevo email delivery failed for " << recipient << endl;
        }
    }).detach();
}

int main() {
    crow::SimpleApp app;

    CROW_ROUTE(app, "/api/signup").methods("POST"_method, "OPTIONS"_method)
    ([](const crow::request& req) {
        if (req.method == "OPTIONS"_method) {
            crow::response res;
            addCorsHeaders(res);
            return res;
        }
        auto body = crow::json::load(req.body);
        if (!body) {
            return crow::response(400, "Invalid JSON");
        }

        string role = body["role"].s();
        string username = body["username"].s();
        string password = body["password"].s();
        string email = body["email"].s();
        string workerId = body.has("workerId") ? string(body["workerId"].s()) : "";

        unique_ptr<Person> newPerson;
        if (role == "Admin") {
            newPerson = make_unique<Admin>(username, password, email, workerId);
        } else if (role == "Employee") {
            newPerson = make_unique<Employee>(username, password, email, workerId);
        } else {
            newPerson = make_unique<User>(username, password, email, "");
        }

        crow::json::wvalue result;
        if (newPerson->alreadyExists()) {
            result["success"] = false;
            result["message"] = "Username, email, or ID already exists.";
            crow::response res{result};
            addCorsHeaders(res);
            return res;
        }

        newPerson->credentials();
        result["success"] = true;
        crow::response res{result};
        addCorsHeaders(res);
        return res;
    });

    CROW_ROUTE(app, "/api/login").methods("POST"_method, "OPTIONS"_method)
    ([](const crow::request& req) {
        if (req.method == "OPTIONS"_method) {
            crow::response res;
            addCorsHeaders(res);
            return res;
        }
        auto body = crow::json::load(req.body);
        if (!body) {
            return crow::response(400, "Invalid JSON");
        }

        string username = body["username"].s();
        string password = body["password"].s();
        double latitude = body.has("latitude") ? body["latitude"].d() : 0.0;
        double longitude = body.has("longitude") ? body["longitude"].d() : 0.0;

        // The login form doesn't ask for a role, so try each role's file
        // until one recognizes this username.
        vector<unique_ptr<Person>> candidates;
        candidates.push_back(make_unique<Admin>(username, password, "", ""));
        candidates.push_back(make_unique<Employee>(username, password, "", ""));
        candidates.push_back(make_unique<User>(username, password, "", ""));

        LocationChecker locChecker;
        crow::json::wvalue result;

        for (auto& person : candidates) {
            if (person->checkAttemptNumber()) {
                bool newLocation = locChecker.isNewLocation(
                    person->getLastLat(), person->getLastLong(),
                    latitude, longitude);
                person->saveLoginLocation(latitude, longitude);

                result["success"] = true;
                result["role"] = person->roleName();
                result["newLocation"] = newLocation;

                queueEmail(person->getEmail(), "Successful login",
                    "<p>Your " + person->roleName() +
                    " account was used to log in successfully.</p>");

                if (newLocation && (person->roleName() == "Admin" ||
                                    person->roleName() == "Employee")) {
                    queueEmail(person->getEmail(), "New login location detected",
                        "<p>Your account was used to log in from a different location.</p>");
                }
                crow::response res{result};
                addCorsHeaders(res);
                return res;
            }
            if (person->getAttempts() == 5) {
                queueEmail(person->getEmail(), "Login warning",
                    "<p>There has been a fifth unsuccessful login attempt on your account.</p>");
            }
            if (person->isLocked()) {
                result["success"] = false;
                result["locked"] = true;
                result["message"] = "Account locked after too many failed attempts.";
                if (person->getAttempts() == 6) {
                    queueEmail(person->getEmail(), "Account locked",
                        "<p>Your account has been locked after too many unsuccessful login attempts.</p>");
                }
                crow::response res{result};
                addCorsHeaders(res);
                return res;
            }
        }

        result["success"] = false;
        result["message"] = "Incorrect username or password.";
        crow::response res{result};
        addCorsHeaders(res);
        return res;
    });

    app.port(18080).multithreaded().run();
    return 0;
}