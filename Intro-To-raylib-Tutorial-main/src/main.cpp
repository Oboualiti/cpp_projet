#include <raylib.h>
#include <algorithm>
#include <vector>
#include <memory>
#include <cstdlib>
#include <ctime>
#include <cmath>

constexpr int SCREEN_WIDTH = 1600;
constexpr int SCREEN_HEIGHT = 700;
constexpr int ROAD_HEIGHT = 140;
constexpr int LANE_HEIGHT = 45;
constexpr float VEHICLE_WIDTH = 90.0f;
constexpr float VEHICLE_HEIGHT = 40.0f;
constexpr float SAFE_DISTANCE = 45.0f;
constexpr int ROAD_Y_TOP = 110;
constexpr int ROAD_Y_BOTTOM = 280;

class TrafficLight {
private:
    Rectangle box;
    float timer;
    bool red;
    float cycleTime;
public:
    TrafficLight(float x, float y, float cycle = 5.0f)
        : box({ x, y, 20, 60 }), timer(0.0f), red(true), cycleTime(cycle) {
    }
    void Update(float delta) {
        timer += delta;
        if (timer >= cycleTime) {
            timer = 0.0f;
            red = !red;
        }
    }
    void Draw() const {
        DrawRectangleRec(box, DARKGRAY);
        DrawCircle((int)(box.x + 10), (int)(box.y + 15), 8.0f, red ? RED : Fade(RED, 0.3f));
        DrawCircle((int)(box.x + 10), (int)(box.y + 45), 8.0f, !red ? GREEN : Fade(GREEN, 0.3f));
    }
    bool IsRed() const { return red; }
    float GetStopLineX(bool rightToLeft) const {
        return rightToLeft ? (box.x - 40) : (box.x + box.width + 40);
    }
};

class Vehicle {
protected:
    float x, y, targetY;
    float speed;
    Color color;
    bool moving;
    bool ambulance;
    bool dirRight;
    bool changedLane;
    Texture2D texture{};
    bool forcedStop;
public:
    float laneChangeAdvance;
    bool laneChangeInProgress;
    Vehicle(float startX, float startY, float spd, Color col, bool dir = true, bool amb = false)
        : x(startX), y(startY), targetY(startY), speed(spd),
        color(col), moving(true), ambulance(amb), dirRight(dir), changedLane(false),
        forcedStop(false), laneChangeAdvance(0.0f), laneChangeInProgress(false) {
    }
    virtual ~Vehicle() { UnloadTexture(texture); }
    virtual void Update(bool stopForRed = false) {
        if (moving && !stopForRed && !forcedStop) x += dirRight ? speed : -speed;
        if (fabs(targetY - y) > 0.5f)
            y += (targetY - y) * 0.08f;
        else
            y = targetY;
    }
    virtual void Draw() const {
        Rectangle source = { 0, 0, (float)texture.width, (float)texture.height };
        Rectangle dest = { x + VEHICLE_WIDTH / 2, y + VEHICLE_HEIGHT / 2, VEHICLE_HEIGHT, VEHICLE_WIDTH };
        Vector2 origin = { VEHICLE_HEIGHT / 2, VEHICLE_WIDTH / 2 };
        float rotation = dirRight ? 90.0f : -90.0f;
        DrawTexturePro(texture, source, dest, origin, rotation, WHITE);
    }
    bool IsOffScreen() const { return dirRight ? x > SCREEN_WIDTH + 200 : x < -200; }
    bool IsAmbulance() const { return ambulance; }
    float GetX() const { return x; }
    float GetY() const { return y; }
    void SetX(float newX) { x = newX; }
    void SetMoving(bool state) { moving = state; }
    bool IsMoving() const { return moving; }
    void SetTargetY(float newY) { targetY = newY; }
    float GetTargetY() const { return targetY; }
    bool HasChangedLane() const { return changedLane; }
    void SetChangedLane(bool v) { changedLane = v; }
    void SetForcedStop(bool stop) { forcedStop = stop; }
    bool IsForcedStop() const { return forcedStop; }
};

class Ambulance : public Vehicle {
public:
    bool atHospital;
    float hospitalTimer;
    Ambulance(float startX, float startY, float spd, bool dirRight = false)
        : Vehicle(startX, startY, spd, RAYWHITE, dirRight, true), atHospital(false), hospitalTimer(0.0f) {
        texture = LoadTexture("ambulance.png");
    }

    void Update(bool stopForRed = false) override {
        if (!atHospital) {
            Vehicle::Update(false); // ignore forced stop
        }
        else {
            hospitalTimer += GetFrameTime();
            if (hospitalTimer > 5.0f) {
                x -= speed * 2.5f;
            }
        }
    }
};

class Car : public Vehicle {
public:
    Car(float startX, float startY, float spd, Color col, bool dirRight = true, const char* imageFile = "car.png")
        : Vehicle(startX, startY, spd, col, dirRight) {
        texture = LoadTexture(imageFile);
    }
};

class Road {
public:
    void Draw() const {
        DrawRectangle(0, 0, SCREEN_WIDTH, ROAD_Y_TOP - 25, DARKGREEN);
        DrawRectangle(0, ROAD_Y_BOTTOM + ROAD_HEIGHT + 20,
            SCREEN_WIDTH, SCREEN_HEIGHT - (ROAD_Y_BOTTOM + ROAD_HEIGHT + 20), DARKGREEN);
        DrawRectangle(0, ROAD_Y_TOP, SCREEN_WIDTH, ROAD_HEIGHT, { 40, 40, 40, 255 });
        DrawRectangle(0, ROAD_Y_BOTTOM, SCREEN_WIDTH, ROAD_HEIGHT, { 40, 40, 40, 255 });
        for (int i = 1; i < 3; i++) {
            DrawLine(0, ROAD_Y_TOP + i * LANE_HEIGHT, SCREEN_WIDTH, ROAD_Y_TOP + i * LANE_HEIGHT, Fade(WHITE, 0.7f));
            DrawLine(0, ROAD_Y_BOTTOM + i * LANE_HEIGHT, SCREEN_WIDTH, ROAD_Y_BOTTOM + i * LANE_HEIGHT, Fade(WHITE, 0.7f));
        }
        DrawRectangle(0, ROAD_Y_TOP - 20, SCREEN_WIDTH, 20, GRAY);
        DrawRectangle(0, ROAD_Y_BOTTOM + ROAD_HEIGHT, SCREEN_WIDTH, 20, GRAY);
        for (int i = 0; i < SCREEN_WIDTH; i += 80) {
            DrawRectangle(i, ROAD_Y_TOP + (ROAD_HEIGHT / 2) - 3, 40, 6, YELLOW);
            DrawRectangle(i, ROAD_Y_BOTTOM + (ROAD_HEIGHT / 2) - 3, 40, 6, YELLOW);
        }
    }
};

class Simulation {
private:
    std::vector<std::unique_ptr<Vehicle>> vehiclesTop;
    std::vector<std::unique_ptr<Vehicle>> vehiclesBottom;
    TrafficLight lightTop;
    TrafficLight lightBottom;
    Road road;
    Texture2D hospitalTexture{};
    float laneYTop[3];
    float laneYBottom[3];
    float carSpawnTimerTop, carSpawnTimerBottom;
    float ambulanceSpawnTimerTop, ambulanceSpawnTimerBottom;
    int ambulanceCountTop, ambulanceCountBottom;
    const char* carImages[5] = { "car.png", "cars.png", "car2.png", "car3.png", "car4.png" };
    Sound siren{};

    // NEW: flashing screen alert state
    bool ambulanceActive = false;
    float screenAlertTimer = 0.0f;
    bool screenAlertOn = false;

public:
    Simulation() :
        lightTop(SCREEN_WIDTH / 2 - 80, ROAD_Y_TOP - 80, 5.0f),
        lightBottom(SCREEN_WIDTH / 2 - 150, ROAD_Y_BOTTOM + ROAD_HEIGHT + 20, 5.0f),
        carSpawnTimerTop(0.0f), carSpawnTimerBottom(0.0f),
        ambulanceSpawnTimerTop(0.0f), ambulanceSpawnTimerBottom(0.0f),
        ambulanceCountTop(0), ambulanceCountBottom(0) {
        for (int i = 0; i < 3; i++) {
            laneYTop[i] = (float)ROAD_Y_TOP + 10.0f + i * (float)LANE_HEIGHT;
            laneYBottom[i] = (float)ROAD_Y_BOTTOM + 10.0f + i * (float)LANE_HEIGHT;
        }
    }

    void Init() {
        siren = LoadSound("siren.wav");
        srand((unsigned int)time(nullptr));
        for (int lane = 0; lane < 3; lane++) {
            Color c = { (unsigned char)GetRandomValue(80, 255),
                        (unsigned char)GetRandomValue(80, 255),
                        (unsigned char)GetRandomValue(80, 255), 255 };
            int imgIdxTop = GetRandomValue(0, 4);
            int imgIdxBottom = GetRandomValue(0, 4);
            vehiclesTop.push_back(std::make_unique<Car>(
                -200 - lane * 150, laneYTop[lane],
                2.0f + GetRandomValue(0, 5) / 10.0f, c, true, carImages[imgIdxTop]));
            vehiclesBottom.push_back(std::make_unique<Car>(
                SCREEN_WIDTH + 200 + lane * 150, laneYBottom[lane],
                2.0f + GetRandomValue(0, 5) / 10.0f, c, false, carImages[imgIdxBottom]));
        }
        hospitalTexture = LoadTexture("hospital.png");
    }

    void SpawnCarTop() {
        int lane = GetRandomValue(0, 2);
        float speed = 2.0f + GetRandomValue(0, 5) / 10.0f;
        Color c = { (unsigned char)GetRandomValue(80, 255),
                    (unsigned char)GetRandomValue(80, 255),
                    (unsigned char)GetRandomValue(80, 255), 255 };
        float startX = -200;
        int imgIdx = GetRandomValue(0, 4);
        vehiclesTop.push_back(std::make_unique<Car>(startX, laneYTop[lane], speed, c, true, carImages[imgIdx]));
    }

    void SpawnCarBottom() {
        int lane = GetRandomValue(0, 2);
        float speed = 2.0f + GetRandomValue(0, 5) / 10.0f;
        Color c = { (unsigned char)GetRandomValue(80, 255),
                    (unsigned char)GetRandomValue(80, 255),
                    (unsigned char)GetRandomValue(80, 255), 255 };
        float startX = SCREEN_WIDTH + 200;
        int imgIdx = GetRandomValue(0, 4);
        vehiclesBottom.push_back(std::make_unique<Car>(startX, laneYBottom[lane], speed, c, false, carImages[imgIdx]));
    }

    void SpawnAmbulanceTop() {
        PlaySound(siren);
        vehiclesTop.push_back(std::make_unique<Ambulance>(
            -200, laneYTop[1], 4.5f, true));
        ambulanceCountTop++;
        ambulanceSpawnTimerTop = 0.0f;
    }

    void SpawnAmbulanceBottom() {
        PlaySound(siren);
        vehiclesBottom.push_back(std::make_unique<Ambulance>(
            SCREEN_WIDTH + 200, laneYBottom[1], 4.5f, false));
        ambulanceCountBottom++;
        ambulanceSpawnTimerBottom = 0.0f;
    }

    void Update(float delta) {
        carSpawnTimerTop += delta;
        if (carSpawnTimerTop >= GetRandomValue(3, 5)) {
            carSpawnTimerTop = 0.0f;
            SpawnCarTop();
        }
        carSpawnTimerBottom += delta;
        if (carSpawnTimerBottom >= GetRandomValue(3, 5)) {
            carSpawnTimerBottom = 0.0f;
            SpawnCarBottom();
        }

        lightTop.Update(delta);
        lightBottom.Update(delta);

        vehiclesTop.erase(
            std::remove_if(vehiclesTop.begin(), vehiclesTop.end(),
                [](const std::unique_ptr<Vehicle>& v) { return v->IsOffScreen(); }),
            vehiclesTop.end()
        );
        vehiclesBottom.erase(
            std::remove_if(vehiclesBottom.begin(), vehiclesBottom.end(),
                [](const std::unique_ptr<Vehicle>& v) { return v->IsOffScreen(); }),
            vehiclesBottom.end()
        );

        // Ambulance yield for top road
        Vehicle* ambulanceT = nullptr;
        for (auto& v2 : vehiclesTop)
            if (v2->IsAmbulance()) ambulanceT = v2.get();

        for (size_t i = 0; i < vehiclesTop.size(); ++i) {
            auto& v = vehiclesTop[i];
            if (!v->IsAmbulance() && ambulanceT && v->GetTargetY() == laneYTop[1] && !v->HasChangedLane()
                && fabs(v->GetX() - ambulanceT->GetX()) < 210) {
                v->SetTargetY(GetRandomValue(0, 1) == 0 ? laneYTop[0] : laneYTop[2]);
                v->SetChangedLane(true);
            }
            if (v->IsAmbulance()) {
                static_cast<Ambulance*>(v.get())->Update();
                continue;
            }
            bool stopForRed = false;
            float stopX = lightTop.GetStopLineX(false);
            if (lightTop.IsRed() && fabs(v->GetX() - stopX) < 50) stopForRed = true;
            if (!stopForRed) {
                for (size_t j = 0; j < vehiclesTop.size(); ++j) {
                    if (i == j) continue;
                    auto& other = vehiclesTop[j];
                    if (v->GetTargetY() == other->GetTargetY() && other->GetX() > v->GetX()) {
                        float front = other->GetX() - VEHICLE_WIDTH;
                        if (front - v->GetX() < SAFE_DISTANCE) { stopForRed = true; break; }
                    }
                }
            }
            v->SetForcedStop(stopForRed);
            v->Update(v->IsForcedStop());
        }

        // Bottom route
        Vehicle* ambulanceB = nullptr;
        for (auto& v2 : vehiclesBottom)
            if (v2->IsAmbulance()) ambulanceB = v2.get();

        // If an ambulance is approaching hospital, make cars in hospital zone vacate
        if (ambulanceB) {
            for (auto& v2 : vehiclesBottom) {
                if (!v2->IsAmbulance()
                    && fabs(v2->GetTargetY() - laneYBottom[2]) < 3.0f
                    && v2->GetX() < 80        // hospital vicinity
                    && !v2->HasChangedLane()
                    && ambulanceB->GetX() < v2->GetX() + 220) { // ambulance close behind
                    // Move car to a free lane (try lane 0 then 1)
                    float newLaneY = (GetRandomValue(0, 1) == 0) ? laneYBottom[0] : laneYBottom[1];
                    v2->SetTargetY(newLaneY);
                    v2->SetChangedLane(true);
                }
            }
        }

        for (size_t i = 0; i < vehiclesBottom.size(); ++i) {
            auto& v = vehiclesBottom[i];

            if (!v->IsAmbulance() && ambulanceB && v->GetTargetY() == laneYBottom[1] && !v->HasChangedLane()
                && fabs(v->GetX() - ambulanceB->GetX()) < 210) {
                v->SetTargetY(GetRandomValue(0, 1) == 0 ? laneYBottom[0] : laneYBottom[2]);
                v->SetChangedLane(true);
            }

            if (v->IsAmbulance()) {
                Ambulance* amb = static_cast<Ambulance*>(v.get());

                // Hospital arrival logic with occupancy check to avoid parking over cars
                if (!amb->atHospital && amb->GetX() < 140 && fabs(amb->GetTargetY() - laneYBottom[1]) < 3.0f) {
                    amb->SetTargetY(laneYBottom[2]); // move towards hospital lane
                }

                // Detect if hospital parking area (x <= 30) is occupied by a car
                bool hospitalOccupied = false;
                Vehicle* occupant = nullptr;
                for (auto& occ : vehiclesBottom) {
                    if (!occ->IsAmbulance()
                        && fabs(occ->GetTargetY() - laneYBottom[2]) < 3.0f
                        && occ->GetX() <= 40) {
                        hospitalOccupied = true;
                        occupant = occ.get();
                        break;
                    }
                }

                // If occupied and ambulance near, stop and wait; do not set atHospital yet
                if (!amb->atHospital && hospitalOccupied && amb->GetX() <= 80) {
                    amb->SetMoving(false);
                }
                else if (!amb->atHospital && !hospitalOccupied && amb->GetX() <= 28
                    && fabs(amb->GetY() - laneYBottom[2]) < 2.0f) {
                    amb->SetX(18);
                    amb->SetMoving(false);
                    amb->atHospital = true;
                    amb->hospitalTimer = 0.0f;
                }
                else if (!amb->atHospital && !hospitalOccupied && !amb->IsMoving()) {
                    // Resume movement if area cleared
                    amb->SetMoving(true);
                }

                // If the occupant is still there and we asked it to leave hospital lane but it hasn't changed lane, force lane change
                if (hospitalOccupied && occupant && !occupant->HasChangedLane()) {
                    float newLaneY = (GetRandomValue(0, 1) == 0) ? laneYBottom[0] : laneYBottom[1];
                    occupant->SetTargetY(newLaneY);
                    occupant->SetChangedLane(true);
                }

                // After hospital wait time
                if (amb->atHospital && amb->GetX() <= 18) {
                    amb->hospitalTimer += GetFrameTime();
                    if (amb->hospitalTimer > 5.0f) {
                        amb->SetMoving(true);
                        amb->SetX(amb->GetX() - 2.2f);
                    }
                }

                amb->Update();
                continue;
            }

            // Usual stop/collision/traffic logic for cars
            bool stopForRed = false;
            float stopX = lightBottom.GetStopLineX(true);
            if (lightBottom.IsRed() && fabs(v->GetX() - stopX) < 50) stopForRed = true;
            if (!stopForRed) {
                for (size_t j = 0; j < vehiclesBottom.size(); ++j) {
                    if (i == j) continue;
                    auto& other = vehiclesBottom[j];
                    if (v->GetTargetY() == other->GetTargetY()) {
                        if (other->GetX() < v->GetX()) {
                            float front = other->GetX() + VEHICLE_WIDTH;
                            if (v->GetX() - front < SAFE_DISTANCE) { stopForRed = true; break; }
                        }
                    }
                }
            }
            v->SetForcedStop(stopForRed);
            v->Update(v->IsForcedStop());
        }

        // --- Ambulance screen alert logic ---
        ambulanceActive = false;
        for (const auto& v : vehiclesTop) {
            if (v->IsAmbulance()) { ambulanceActive = true; break; }
        }
        if (!ambulanceActive) {
            for (const auto& v : vehiclesBottom) {
                if (v->IsAmbulance()) { ambulanceActive = true; break; }
            }
        }

        if (ambulanceActive) {
            screenAlertTimer += delta;
            if (screenAlertTimer >= 0.5f) { // toggle every 0.5s
                screenAlertOn = !screenAlertOn;
                screenAlertTimer = 0.0f;
            }
        } else {
            screenAlertOn = false;
            screenAlertTimer = 0.0f;
        }
    }

    void call_Emergency(Vector2 location) {//press E to call emergency ambulance
        PlaySound(siren);
        bool topRoad = location.y < SCREEN_HEIGHT / 2;
        int lane = 1;
        float spawnX = topRoad ? -200.0f : SCREEN_WIDTH + 200.0f;

        ambulanceActive = true; // activate flashing screen

        if (topRoad) {
            float minDist = fabs(location.y - laneYTop[0]);
            lane = 0;
            for (int i = 1; i < 3; i++) {
                float d = fabs(location.y - laneYTop[i]);
                if (d < minDist) { minDist = d; lane = i; }
            }
            vehiclesTop.push_back(std::make_unique<Ambulance>(spawnX, laneYTop[lane], 4.5f, true));
            ambulanceCountTop++;
        }
        else {
            float minDist = fabs(location.y - laneYBottom[0]);
            lane = 0;
            for (int i = 1; i < 3; i++) {
                float d = fabs(location.y - laneYBottom[i]);
                if (d < minDist) { minDist = d; lane = i; }
            }
            vehiclesBottom.push_back(std::make_unique<Ambulance>(spawnX, laneYBottom[lane], 4.5f, false));
            ambulanceCountBottom++;
        }
    }

    void Draw() const {
        road.Draw();
        lightTop.Draw();
        lightBottom.Draw();
        DrawTexture(hospitalTexture, 10, ROAD_Y_BOTTOM + ROAD_HEIGHT + 10, WHITE);
        for (auto& v : vehiclesTop) v->Draw();
        for (auto& v : vehiclesBottom) v->Draw();

        // --- Draw flashing red screen alert ---
        if (screenAlertOn) {
            DrawRectangle(0, 0, 20, SCREEN_HEIGHT, Fade(RED, 0.7f));                    // left side
            DrawRectangle(SCREEN_WIDTH - 20, 0, 20, SCREEN_HEIGHT, Fade(RED, 0.7f));     // right side
        }
    }

    ~Simulation() {
        UnloadSound(siren);
        UnloadTexture(hospitalTexture);
    }
};

int main() {
    InitAudioDevice();
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Bidirectional Traffic Simulation: Ambulance to Hospital");
    SetTargetFPS(60);
    Simulation sim;
    sim.Init();
    while (!WindowShouldClose()) {
        float delta = GetFrameTime();
        sim.Update(delta);
        BeginDrawing();
        ClearBackground(SKYBLUE);
        sim.Draw();
        EndDrawing();
        if (IsKeyPressed(KEY_E)) {
            Vector2 emergencyLocation = { (float)(SCREEN_WIDTH / 2), (float)(SCREEN_HEIGHT / 2) };
            sim.call_Emergency(emergencyLocation);
        }
    }
    CloseAudioDevice();
    CloseWindow();
    return 0;
}
