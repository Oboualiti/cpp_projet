#include "raylib.h"
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
        : box({ x, y, 20, 60 }), timer(0.0f), red(true), cycleTime(cycle) {}
    void Update(float delta) {
        timer += delta;
        if (timer >= cycleTime) {
            timer = 0.0f;
            red = !red;
        }
    }
    void Draw() const {
        DrawRectangleRec(box , DARKGRAY);
        DrawCircle(box.x + 10, box.y + 15, 8, red ? RED : Fade(RED, 0.3f));
        DrawCircle(box.x + 10, box.y + 45, 8, !red ? GREEN : Fade(GREEN, 0.3f));
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
    Texture2D texture;
    bool forcedStop;
public:
    float laneChangeAdvance;
    bool laneChangeInProgress;
    Vehicle(float startX, float startY, float spd, Color col, bool dir = true, bool amb = false)
        : x(startX), y(startY), targetY(startY), speed(spd),
          color(col), moving(true), ambulance(amb), dirRight(dir), changedLane(false),
          forcedStop(false), laneChangeAdvance(0.0f), laneChangeInProgress(false) {}
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
        Rectangle dest = { x + VEHICLE_WIDTH/2, y + VEHICLE_HEIGHT/2, VEHICLE_HEIGHT, VEHICLE_WIDTH };
        Vector2 origin = { VEHICLE_HEIGHT/2, VEHICLE_WIDTH/2 };
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
            Vehicle::Update(false); // always ignore forced stop!
        } else {
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
    Texture2D hospitalTexture;
    float laneYTop[3];
    float laneYBottom[3];
    float carSpawnTimerTop, carSpawnTimerBottom;
    float ambulanceSpawnTimerTop, ambulanceSpawnTimerBottom;
    int ambulanceCountTop, ambulanceCountBottom;
    const char* carImages[5] = { "car.png", "cars.png", "car2.png", "car3.png", "car4.png" };
public:
    Simulation() :
        lightTop(SCREEN_WIDTH / 2 - 80, ROAD_Y_TOP - 80, 5.0f),
        lightBottom(SCREEN_WIDTH / 2 - 150, ROAD_Y_BOTTOM + ROAD_HEIGHT + 20 , 5.0f),
        carSpawnTimerTop(0.0f), carSpawnTimerBottom(0.0f),
        ambulanceSpawnTimerTop(0.0f), ambulanceSpawnTimerBottom(0.0f),
        ambulanceCountTop(0), ambulanceCountBottom(0)
    {
        for (int i = 0; i < 3; i++) {
            laneYTop[i] = ROAD_Y_TOP + 10 + i * LANE_HEIGHT;
            laneYBottom[i] = ROAD_Y_BOTTOM + 10 + i * LANE_HEIGHT;
        }
    }
    void Init() {
        srand(time(nullptr));
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
        vehiclesTop.push_back(std::make_unique<Ambulance>(
            -200, laneYTop[1], 4.5f, true));
        ambulanceCountTop++;
        ambulanceSpawnTimerTop = 0.0f;
    }
    void SpawnAmbulanceBottom() {
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
        ambulanceSpawnTimerTop += delta;
        if (ambulanceSpawnTimerTop >= GetRandomValue(15, 20)) {
            SpawnAmbulanceTop();
        }
        ambulanceSpawnTimerBottom += delta;
        if (ambulanceSpawnTimerBottom >= GetRandomValue(15, 20)) {
            SpawnAmbulanceBottom();
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
            // Cars in CENTER lane yield if ambulance is near, anytime
            if (!v->IsAmbulance() && ambulanceT && v->GetTargetY() == laneYTop[1] && !v->HasChangedLane() && fabs(v->GetX() - ambulanceT->GetX()) < 210) {
                v->SetTargetY(GetRandomValue(0, 1) == 0 ? laneYTop[0] : laneYTop[2]);
                v->SetChangedLane(true);
            }
            if (v->IsAmbulance()) {
                Ambulance* amb = static_cast<Ambulance*>(v.get());
                amb->Update();
                continue;
            }
            bool stopForRed = false;
            float stopX = lightTop.GetStopLineX(false);
            if (lightTop.IsRed()) {
                if (fabs(v->GetX() - stopX) < 50) stopForRed = true;
            }
            if (!stopForRed) {
                for (size_t j = 0; j < vehiclesTop.size(); ++j) {
                    if (i == j) continue;
                    auto& other = vehiclesTop[j];
                    if (v->GetTargetY() == other->GetTargetY()) {
                        if (other->GetX() > v->GetX()) {
                            float front = other->GetX() - VEHICLE_WIDTH;
                            if (front - v->GetX() < SAFE_DISTANCE) {
                                stopForRed = true;
                                break;
                            }
                        }
                    }
                }
            }
            v->SetForcedStop(stopForRed);
            v->Update(v->IsForcedStop());
        }
        // Find ambulance in bottom route
        Vehicle* ambulanceB = nullptr;
        for (auto& v2 : vehiclesBottom)
            if (v2->IsAmbulance()) ambulanceB = v2.get();
        for (size_t i = 0; i < vehiclesBottom.size(); ++i) {
            auto& v = vehiclesBottom[i];
            // Cars in CENTER lane yield if ambulance is near, anytime
            if (!v->IsAmbulance() && ambulanceB && v->GetTargetY() == laneYBottom[1] && !v->HasChangedLane() && fabs(v->GetX() - ambulanceB->GetX()) < 210) {
                v->SetTargetY(GetRandomValue(0, 1) == 0 ? laneYBottom[0] : laneYBottom[2]);
                v->SetChangedLane(true);
            }
            // Ambulance smooth hospital logic (stay 5s)
            if (v->IsAmbulance()) {
                Ambulance* amb = static_cast<Ambulance*>(v.get());
                if (!amb->atHospital && amb->GetX() < 140 && fabs(amb->GetTargetY() - laneYBottom[1]) < 3.0f) {
                    amb->SetTargetY(laneYBottom[2]);
                }
                if (!amb->atHospital && amb->GetX() <= 28 && fabs(amb->GetY() - laneYBottom[2]) < 2.0f) {
                    amb->SetX(18);
                    amb->SetMoving(false);
                    amb->atHospital = true;
                    amb->hospitalTimer = 0;
                }
                if (amb->atHospital && amb->GetX() <= 18) {
                    amb->hospitalTimer += GetFrameTime();
                    if (amb->hospitalTimer > 5) {
                        amb->SetMoving(true);
                        amb->SetX(amb->GetX() - 2.2f);
                    }
                }
                amb->Update();
                continue;
            }
            // Usual stop/collision/traffic logic:
            bool stopForRed = false;
            float stopX = lightBottom.GetStopLineX(true);
            if (lightBottom.IsRed()) {
                if (fabs(v->GetX() - stopX) < 50) stopForRed = true;
            }
            if (!stopForRed) {
                for (size_t j = 0; j < vehiclesBottom.size(); ++j) {
                    if (i == j) continue;
                    auto& other = vehiclesBottom[j];
                    if (v->GetTargetY() == other->GetTargetY()) {
                        if (other->GetX() < v->GetX()) {
                            float front = other->GetX() + VEHICLE_WIDTH;
                            if (v->GetX() - front < SAFE_DISTANCE) {
                                stopForRed = true;
                                break;
                            }
                        }
                    }
                }
            }
            v->SetForcedStop(stopForRed);
            v->Update(v->IsForcedStop());
        }
    }
    void Draw() const {
        road.Draw();
        lightTop.Draw();
        lightBottom.Draw();
        DrawTexture(hospitalTexture, 10, ROAD_Y_BOTTOM + ROAD_HEIGHT + 10, WHITE);
        for (auto& v : vehiclesTop) v->Draw();
        for (auto& v : vehiclesBottom) v->Draw();
    }
};

int main() {
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
    }
    CloseWindow();
    return 0;
}
