#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <cmath>
#include <memory>
#include <cstdlib>
#include <thread>
#include <vector>
#include <ctime>
#include <iomanip>
#include "crow_all.h"
#include <windows.h>
#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifdef OPTIONS
#undef OPTIONS
#endif

using namespace std;

// ============================================================
// Database classes - APPENDs a new record with timestamp (0)
// Format: username,password,email,id,attempts,lastLat,lastLong,lastTimestamp
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
            // Trailing 0,0,0,0 = attempts, lastLat, lastLong, lastTimestamp
            file << name << "," << password << "," << email << ","
                 << id << ",0,0,0,0" << "\n";
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
                 << id << ",0,0,0,0" << "\n";
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
                 << id << ",0,0,0,0" << "\n";
            file.close();
        }
    }
};

// ============================================================
// LocationChecker - calculates distance and travel speed
// ============================================================

class LocationChecker {
private:
    double toRadians(double degree) const {
        return degree * M_PI / 180.0;
    }

    // Haversine formula to compute distance in kilometers
    double calculateDistanceKm(double lat1, double lon1, double lat2, double lon2) const {
        double dLat = toRadians(lat2 - lat1);
        double dLon = toRadians(lon2 - lon1);
        double a = sin(dLat / 2.0) * sin(dLat / 2.0) +
                   cos(toRadians(lat1)) * cos(toRadians(lat2)) *
                   sin(dLon / 2.0) * sin(dLon / 2.0);
        a = max(0.0, min(1.0, a));
        double c = 2.0 * atan2(sqrt(a), sqrt(1.0 - a));
        return 6371.0 * c; // Earth's mean radius in km
    }

public:
    bool hasValidCoordinates(double latitude, double longitude) const {
        return isfinite(latitude) && isfinite(longitude) &&
               latitude >= -90.0 && latitude <= 90.0 &&
               longitude >= -180.0 && longitude <= 180.0 &&
               !(latitude == 0.0 && longitude == 0.0);
    }

    bool isNewLocation(double lastLat, double lastLong,
                        double currentLat, double currentLong) {
        if (!hasValidCoordinates(lastLat, lastLong) ||
            !hasValidCoordinates(currentLat, currentLong)) {
            return false;
        }
        double latDiff = fabs(lastLat - currentLat);
        double longDiff = fabs(lastLong - currentLong);
        return (latDiff > 0.5 || longDiff > 0.5);
    }

    // Checks if the speed required to travel between logins exceeds human capabilities
    bool isImpossibleTravel(double lastLat, double lastLong, time_t lastTime,
                            double currentLat, double currentLong, time_t currentTime,
                            double maxSpeedKmH = 800.0) {
        if (!hasValidCoordinates(lastLat, lastLong) ||
            !hasValidCoordinates(currentLat, currentLong) ||
            lastTime == 0 || currentTime <= lastTime) {
            return false; // No previous baseline on record
        }

        double distanceKm = calculateDistanceKm(lastLat, lastLong, currentLat, currentLong);
        double timeDiffSeconds = difftime(currentTime, lastTime);

        // Ignore minor location jitter within 30 km
        if (distanceKm < 30.0) {
            return false;
        }

        // Extremely fast relocation in under 10 seconds over >30 km
        if (timeDiffSeconds <= 10) {
            return true;
        }

        double timeDiffHours = timeDiffSeconds / 3600.0;
        double speedKmH = distanceKm / timeDiffHours;

        // Flag if required travel speed exceeds airliner speed (~800 km/h)
        return speedKmH > maxSpeedKmH;
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
    string storedPassword;
    string storedEmail;
    int attempts;
    double lastLat;
    double lastLong;
    time_t lastTimestamp;

    virtual string filename() const = 0;

    bool lookupByUsername() {
        string line;
        ifstream file(filename());
        if (!file.is_open()) return false;

        while (getline(file, line)) {
            stringstream iss(line);
            string fUsername, fPassword, fEmail, fId, fAttempts, fLat, fLong, fTime;
            getline(iss, fUsername, ',');
            getline(iss, fPassword, ',');
            getline(iss, fEmail, ',');
            getline(iss, fId, ',');
            getline(iss, fAttempts, ',');
            getline(iss, fLat, ',');
            getline(iss, fLong, ',');
            getline(iss, fTime, ',');

            if (fUsername == username) {
                storedPassword = fPassword;
                storedEmail = fEmail;
                attempts = fAttempts.empty() ? 0 : stoi(fAttempts);
                lastLat = fLat.empty() ? 0.0 : stod(fLat);
                lastLong = fLong.empty() ? 0.0 : stod(fLong);
                lastTimestamp = fTime.empty() ? 0 : stoll(fTime);
                file.close();
                return true;
            }
        }
        file.close();
        return false;
    }

    void updateRecord(int newAttempts, double newLat, double newLong, time_t newTimestamp) {
        ifstream inFile(filename());
        stringstream buffer;
        string line;

        while (inFile.is_open() && getline(inFile, line)) {
            stringstream iss(line);
            string fUsername, fPassword, fEmail, fId, fAttempts, fLat, fLong, fTime;
            getline(iss, fUsername, ',');
            getline(iss, fPassword, ',');
            getline(iss, fEmail, ',');
            getline(iss, fId, ',');
            getline(iss, fAttempts, ',');
            getline(iss, fLat, ',');
            getline(iss, fLong, ',');
            getline(iss, fTime, ',');

            if (fUsername == username) {
                buffer << fUsername << "," << fPassword << "," << fEmail << ","
                       << fId << "," << newAttempts << "," << newLat << ","
                       << newLong << "," << newTimestamp << "\n";
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
          attempts(0), lastLat(0.0), lastLong(0.0), lastTimestamp(0) {}

    virtual void credentials() = 0;
    virtual string roleName() const = 0;
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

    bool checkAttemptNumber() {
        if (!lookupByUsername()) {
            return false;
        }
        if (attempts >= 6) {
            cout << "Account locked: too many failed attempts." << endl;
            return false;
        }
        if (storedPassword == password) {
            updateRecord(0, lastLat, lastLong, lastTimestamp);
            attempts = 0;
            return true;
        }
        attempts++;
        updateRecord(attempts, lastLat, lastLong, lastTimestamp);
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
    time_t getLastTimestamp() const { return lastTimestamp; }

    void saveLoginLocation(double latitude, double longitude, time_t currentTimestamp) {
        if (!isfinite(latitude) || !isfinite(longitude) ||
            latitude < -90.0 || latitude > 90.0 ||
            longitude < -180.0 || longitude > 180.0 ||
            (latitude == 0.0 && longitude == 0.0)) {
            return;
        }
        lastLat = latitude;
        lastLong = longitude;
        lastTimestamp = currentTimestamp;
        updateRecord(0, lastLat, lastLong, lastTimestamp);
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

string locationText(double latitude, double longitude) {
    if (!isfinite(latitude) || !isfinite(longitude) ||
        (latitude == 0.0 && longitude == 0.0)) {
        return "Location unavailable";
    }
    ostringstream location;
    location << fixed << setprecision(6)
             << "Latitude: " << latitude
             << ", Longitude: " << longitude;
    return location.str();
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
    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    if (received) {
        WinHttpQueryHeaders(request,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusCodeSize,
            WINHTTP_NO_HEADER_INDEX);
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    if (received && statusCode >= 200 && statusCode < 300) {
        cout << "Brevo email accepted for delivery to " << recipient << endl;
        return true;
    }
    cerr << "Brevo email request failed with HTTP status " << statusCode << endl;
    return false;
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
        time_t currentTime = time(nullptr); // Current login timestamp

        vector<unique_ptr<Person>> candidates;
        candidates.push_back(make_unique<Admin>(username, password, "", ""));
        candidates.push_back(make_unique<Employee>(username, password, "", ""));
        candidates.push_back(make_unique<User>(username, password, "", ""));

        LocationChecker locChecker;
        crow::json::wvalue result;

        for (auto& person : candidates) {
            if (person->checkAttemptNumber()) {
                double lastLat = person->getLastLat();
                double lastLong = person->getLastLong();
                time_t lastTime = person->getLastTimestamp();

                bool newLocation = locChecker.isNewLocation(lastLat, lastLong, latitude, longitude);
                
                // Detect impossible speed / rapid relocation between logins
                bool rapidLocationChange = locChecker.isImpossibleTravel(
                    lastLat, lastLong, lastTime, latitude, longitude, currentTime
                );

                person->saveLoginLocation(latitude, longitude, currentTime);

                result["success"] = true;
                result["role"] = person->roleName();
                result["newLocation"] = newLocation;
                result["impossibleTravel"] = rapidLocationChange;

                // 1. Send critical alarm email if impossible travel / rapid relocation is detected
                if (rapidLocationChange) {
                    queueEmail(person->getEmail(), 
                        "URGENT SECURITY ALERT: Suspicious Rapid Login Location Change",
                        "<h2 style='color:red;'>Security Alarm: Impossible Travel Detected</h2>"
                        "<p>Your account was logged into from a distant location in an impossibly short period of time.</p>"
                        "<p>Previous location: " + locationText(lastLat, lastLong) + "</p>"
                        "<p>Current location: " + locationText(latitude, longitude) + "</p>"
                        "<p>If this was not you, please change your password immediately.</p>");
                }
                // 2. Standard location notification for Admin / Employee
                else if (newLocation && (person->roleName() == "Admin" || person->roleName() == "Employee")) {
                    queueEmail(person->getEmail(), "New login location detected",
                        "<p>Your account was used to log in from a different location.</p>"
                        "<p>Previous location: " + locationText(lastLat, lastLong) + "</p>"
                        "<p>Current location: " + locationText(latitude, longitude) + "</p>");
                }
                // 3. Regular success notification
                else {
                    queueEmail(person->getEmail(), "Successful login",
                        "<p>Your " + person->roleName() + " account was used to log in successfully.</p>"
                        "<p>Login location: " + locationText(latitude, longitude) + "</p>");
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