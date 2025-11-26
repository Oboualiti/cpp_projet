#include <raylib.h>
#include <algorithm>
#include <vector>
#include <memory>
#include <cstdlib>
#include <ctime>
#include <cmath>
#include <iostream>

constexpr int SCREEN_WIDTH = 1600;
constexpr int SCREEN_HEIGHT = 700;
constexpr int ROAD_HEIGHT = 140;
constexpr int LANE_HEIGHT = 45;
constexpr float VEHICLE_WIDTH = 90.0f;
constexpr float VEHICLE_HEIGHT = 40.0f;
constexpr float SAFE_DISTANCE = 45.0f;
constexpr int ROAD_Y_TOP = 110;
constexpr int ROAD_Y_BOTTOM = 280;

// Enum for Ambulance State Machine
enum AmbulanceState {
    PATROL,
    TO_ACCIDENT,
    WAIT_AT_ACCIDENT,
    TO_HOSPITAL,
    WAIT_AT_HOSPITAL,
    LEAVING
};

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
    bool depannage; 
    bool dirRight;
    bool changedLane;
    Texture2D texture{};
    bool forcedStop;
    
public:
    bool isCrashed; 
    bool toBeRemoved; 
    
    // New flags for smooth mechanics
    bool isReckless;        // Will cause accident (Rear car)
    bool isAccidentTarget;  // Victim of accident (Front car)
    bool isTowed;           // Being pulled by tow truck
    bool laneLock;          // Prevents lane changing during accidents
    float towOffsetX;       // Position relative to tow truck

    Vehicle(float startX, float startY, float spd, Color col, bool dir = true, bool amb = false, bool dep = false)
        : x(startX), y(startY), targetY(startY), speed(spd),
        color(col), moving(true), ambulance(amb), depannage(dep), dirRight(dir), changedLane(false),
        forcedStop(false), isCrashed(false), toBeRemoved(false),
        isReckless(false), isAccidentTarget(false), isTowed(false), laneLock(false), towOffsetX(0.0f) {
    }
    virtual ~Vehicle() { UnloadTexture(texture); }
    
    virtual void Update(bool stopForRed = false) {
        // Crashed cars do not move on their own
        if (isCrashed && !isTowed) return;

        // Towed logic handles position in Simulation update or here if we pass the parent, 
        // but simple way: if isTowed, don't do normal movement
        if (isTowed) return;

        // Reckless drivers ignore red lights and speed up
        if (isReckless) {
            stopForRed = false;
            forcedStop = false;
        }

        if (moving && !stopForRed && !forcedStop) x += dirRight ? speed : -speed;
        
        // Lane changing smoothing
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
        
        // If crash, tint red
        Color drawColor = WHITE;
        if (isCrashed) drawColor = RED; 

        DrawTexturePro(texture, source, dest, origin, rotation, drawColor);
    }

    bool IsOffScreen() const { return dirRight ? x > SCREEN_WIDTH + 200 : x < -200; }
    bool IsAmbulance() const { return ambulance; }
    bool IsDepannage() const { return depannage; }
    float GetX() const { return x; }
    float GetY() const { return y; }
    void SetX(float newX) { x = newX; }
    void SetY(float newY) { y = newY; }
    void SetSpeed(float s) { speed = s; }
    float GetSpeed() const { return speed; }
    void SetMoving(bool state) { moving = state; }
    bool IsMoving() const { return moving; }
    void SetTargetY(float newY) { targetY = newY; }
    float GetTargetY() const { return targetY; }
    bool HasChangedLane() const { return changedLane; }
    void SetChangedLane(bool v) { changedLane = v; }
    void SetForcedStop(bool stop) { forcedStop = stop; }
    bool IsForcedStop() const { return forcedStop; }
};

class Car : public Vehicle {
public:
    Car(float startX, float startY, float spd, Color col, bool dirRight = true, const char* imageFile = "car.png")
        : Vehicle(startX, startY, spd, col, dirRight) {
        texture = LoadTexture(imageFile);
    }
};

class Ambulance : public Vehicle {
public:
    AmbulanceState state;
    float stateTimer;
    float accidentX;
    float accidentY;

    Ambulance(float startX, float startY, float spd, bool dirRight = false)
        : Vehicle(startX, startY, spd, RAYWHITE, dirRight, true), 
          state(PATROL), stateTimer(0.0f), accidentX(0), accidentY(0) {
        texture = LoadTexture("ambulance.png");
    }

    void AssignAccident(float accX, float accY) {
        accidentX = accX;
        accidentY = accY;
        state = TO_ACCIDENT;
    }

    void Update(bool stopForRed = false) override {
        switch (state) {
            case PATROL:
                Vehicle::Update(stopForRed);
                break;

            case TO_ACCIDENT:
                // Move towards accident
                if (dirRight) x += speed; else x -= speed;
                
                // STOP LOGIC: strictly check if we reached the safe spot BEHIND accident
                // Accident is at accidentX. We move Right->Left. Stop at accidentX + 160.
                if (x <= accidentX + 160.0f) {
                    x = accidentX + 160.0f; // Snap to position
                    state = WAIT_AT_ACCIDENT;
                    stateTimer = 0.0f;
                }
                break;

            case WAIT_AT_ACCIDENT:
                stateTimer += GetFrameTime();
                if (stateTimer >= 5.0f) {
                    state = TO_HOSPITAL;
                }
                break;

            case TO_HOSPITAL:
                // Move towards Hospital (Left side)
                if (x > 80) {
                    x -= speed; 
                } else {
                    state = WAIT_AT_HOSPITAL;
                    stateTimer = 0.0f;
                }
                break;

            case WAIT_AT_HOSPITAL:
                stateTimer += GetFrameTime();
                if (stateTimer >= 5.0f) {
                    state = LEAVING;
                }
                break;

            case LEAVING:
                x -= speed; 
                break;
        }

        // Lane smoothing
        if (fabs(targetY - y) > 0.5f)
             y += (targetY - y) * 0.08f;
        else
             y = targetY;
    }
};

class Depannage : public Vehicle {
public:
    bool hasPickedUp;
    float targetX;
    float workTimer; 
    bool isWorking;

    Depannage(float startX, float startY, float spd)
        : Vehicle(startX, startY, spd, ORANGE, false, false, true), 
          hasPickedUp(false), targetX(0), workTimer(0.0f), isWorking(false) {
        texture = LoadTexture("depannage.png"); 
    }

    void SetTarget(float tX) {
        targetX = tX;
    }

    void Update(bool stopForRed = false) override {
        if (!hasPickedUp) {
            if (!isWorking) {
                // Moving to accident
                // Stop slightly behind where ambulance was (or accident)
                if (x > targetX + 180) { 
                    x -= speed;
                } else {
                    // Arrived, start working
                    isWorking = true;
                    workTimer = 0.0f;
                }
            } else {
                // Simulation "Working" phase (Hooking up cars)
                workTimer += GetFrameTime();
                if (workTimer > 2.0f) {
                    hasPickedUp = true;
                    isWorking = false;
                }
            }
        } else {
            // Leave with cars
            x -= speed;
        }

         if (fabs(targetY - y) > 0.5f)
             y += (targetY - y) * 0.08f;
        else
             y = targetY;
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

struct Accident {
    bool active;
    bool pending; // Waiting for collision
    float x;
    float y;
    Vehicle* car1; // Front
    Vehicle* car2; // Rear (Aggressor)
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
    const char* carImages[5] = { "car.png", "cars.png", "car2.png", "car3.png", "car4.png" };
    Sound siren{};

    bool ambulanceActive = false;
    float screenAlertTimer = 0.0f;
    bool screenAlertOn = false;
    
    Accident currentAccident;

public:
    Simulation() :
        lightTop(SCREEN_WIDTH / 2 - 80, ROAD_Y_TOP - 80, 5.0f),
        lightBottom(SCREEN_WIDTH / 2 - 150, ROAD_Y_BOTTOM + ROAD_HEIGHT + 20, 5.0f),
        carSpawnTimerTop(0.0f), carSpawnTimerBottom(0.0f) 
    {
        for (int i = 0; i < 3; i++) {
            laneYTop[i] = (float)ROAD_Y_TOP + 10.0f + i * (float)LANE_HEIGHT;
            laneYBottom[i] = (float)ROAD_Y_BOTTOM + 10.0f + i * (float)LANE_HEIGHT;
        }
        currentAccident = { false, false, 0, 0, nullptr, nullptr };
    }

    void Init() {
        siren = LoadSound("siren.wav");
        srand((unsigned int)time(nullptr));
        hospitalTexture = LoadTexture("hospital.png");
    }

    void SpawnCarTop() {
        int lane = GetRandomValue(0, 2);
        float speed = 2.0f + GetRandomValue(0, 5) / 10.0f;
        Color c = { (unsigned char)GetRandomValue(80, 255), (unsigned char)GetRandomValue(80, 255), (unsigned char)GetRandomValue(80, 255), 255 };
        vehiclesTop.push_back(std::make_unique<Car>(-200, laneYTop[lane], speed, c, true, carImages[GetRandomValue(0, 4)]));
    }

    void SpawnCarBottom() {
        int lane = GetRandomValue(0, 2);
        float speed = 2.0f + GetRandomValue(0, 5) / 10.0f;
        Color c = { (unsigned char)GetRandomValue(80, 255), (unsigned char)GetRandomValue(80, 255), (unsigned char)GetRandomValue(80, 255), 255 };
        vehiclesBottom.push_back(std::make_unique<Car>(SCREEN_WIDTH + 200, laneYBottom[lane], speed, c, false, carImages[GetRandomValue(0, 4)]));
    }

    // UPDATED: Smooth accident creation with visual chase
    void TriggerRandomAccident() {
        if (currentAccident.active || currentAccident.pending) return;

        // Find two cars in same lane, close enough to force a crash
        for (size_t i = 0; i < vehiclesBottom.size(); i++) {
            Vehicle* v1 = vehiclesBottom[i].get(); // Potential Rear
            if (v1->IsAmbulance() || v1->IsDepannage() || v1->IsOffScreen()) continue;
            // FIX 2: Never pick cars already towed or crashed for a NEW accident
            if (v1->isTowed || v1->isCrashed) continue;

            for (size_t j = 0; j < vehiclesBottom.size(); j++) {
                if (i == j) continue;
                Vehicle* v2 = vehiclesBottom[j].get(); // Potential Front

                if (v2->IsAmbulance() || v2->IsDepannage() || v2->IsOffScreen()) continue;
                // FIX 2: Never pick cars already towed or crashed for a NEW accident
                if (v2->isTowed || v2->isCrashed) continue;

                // Same lane?
                if (fabs(v1->GetTargetY() - v2->GetTargetY()) < 5.0f) {
                    
                    // Cars move RIGHT to LEFT (Negative X). 
                    // Larger X is BEHIND.
                    if (v1->GetX() > v2->GetX()) {
                        float dist = v1->GetX() - v2->GetX();
                        
                        // Condition: Must be close enough to chase (400) 
                        // BUT far enough to not be instant/overlapping (> 100)
                        if (dist < 400 && dist > 110 && v1->GetX() < SCREEN_WIDTH - 100 && v2->GetX() > 100) {
                            
                            // Setup Pending Accident
                            currentAccident.pending = true;
                            currentAccident.car1 = v2; // Front
                            currentAccident.car2 = v1; // Rear
                            
                            // LOCK VEHICLES
                            v1->isReckless = true;
                            v2->isAccidentTarget = true;
                            v1->laneLock = true; 
                            v2->laneLock = true; 

                            // Make rear car reckless!
                            v1->SetSpeed(v1->GetSpeed() * 2.8f); 
                            v2->SetSpeed(v2->GetSpeed() * 0.4f);
                            
                            std::cout << "Accident Pending initiated!" << std::endl;
                            return;
                        }
                    }
                }
            }
        }
    }

    void CallAmbulance() {
        if (!currentAccident.active && !currentAccident.pending) {
            TriggerRandomAccident(); 
        }

        PlaySound(siren);
        // Spawn ambulance
        auto amb = std::make_unique<Ambulance>(SCREEN_WIDTH + 200, laneYBottom[1], 4.5f, false);
        
        // If accident is active, assign immediately
        if (currentAccident.active) {
            amb->AssignAccident(currentAccident.x, currentAccident.y);
            amb->SetTargetY(currentAccident.y);
        }
        
        vehiclesBottom.push_back(std::move(amb));
        ambulanceActive = true;
    }

    void CallDepannage() {
        if (!currentAccident.active) return;
        auto tow = std::make_unique<Depannage>(SCREEN_WIDTH + 200, currentAccident.y, 3.5f);
        tow->SetTarget(currentAccident.x);
        vehiclesBottom.push_back(std::move(tow));
    }

    void Update(float delta) {
        // --- 1. CLEANUP ORPHANED TOWED CARS ---
        // FIX 1: Before anything else, check if there is an active Tow Truck on screen.
        // If not (or if it's too far gone), mark all "Towed" cars for immediate removal.
        // This prevents the "Tow truck arriving with cars already" bug.
        
        Depannage* ptrTow = nullptr;
        for (auto& v : vehiclesBottom) if (v->IsDepannage()) ptrTow = (Depannage*)v.get();
        
        // If tow truck is missing OR it is about to be deleted (x < -600), 
        // then any cars marked 'isTowed' must be deleted NOW.
        bool towTruckGone = (ptrTow == nullptr) || (ptrTow->GetX() < -600.0f);

        if (towTruckGone) {
            for (auto& v : vehiclesBottom) {
                if (v->isTowed) {
                    v->toBeRemoved = true; 
                }
            }
        }


        carSpawnTimerTop += delta;
        // ADJUSTED TRAFFIC: Spawn every 2.0s - 3.5s
        if (carSpawnTimerTop >= GetRandomValue(20, 35) / 10.0f) { 
            carSpawnTimerTop = 0.0f; 
            SpawnCarTop(); 
        } 
        
        carSpawnTimerBottom += delta;
        // ADJUSTED TRAFFIC: Spawn every 2.0s - 3.5s
        if (carSpawnTimerBottom >= GetRandomValue(20, 35) / 10.0f) { 
            carSpawnTimerBottom = 0.0f; 
            SpawnCarBottom(); 
        }

        // Low chance of random accident
        if (GetRandomValue(0, 1000) < 2) TriggerRandomAccident();

        lightTop.Update(delta);
        lightBottom.Update(delta);

        // --- Remove off screen vehicles ---
        vehiclesTop.erase(std::remove_if(vehiclesTop.begin(), vehiclesTop.end(),
            [](const std::unique_ptr<Vehicle>& v) { return v->IsOffScreen(); }), vehiclesTop.end());
        
        // SAFE REMOVAL: Fixes Segfault and Ghost Accidents
        vehiclesBottom.erase(std::remove_if(vehiclesBottom.begin(), vehiclesBottom.end(),
            [&](const std::unique_ptr<Vehicle>& v) { 
                
                // Keep Depannage truck alive longer
                if (v->IsDepannage()) {
                    return v->GetX() < -600.0f; 
                }
                
                // Do NOT delete cars involved in accident sequence
                if (v->isReckless || v->isAccidentTarget || v->isCrashed || v->isTowed) {
                    // However, if they are WAY off screen, let them go
                    if (v->GetX() > -600.0f && !v->toBeRemoved) return false;
                    
                    // IF we are deleting them now, clear pointers to prevent dangling usage
                    if (v.get() == currentAccident.car1) currentAccident.car1 = nullptr;
                    if (v.get() == currentAccident.car2) currentAccident.car2 = nullptr;
                    
                    // If we delete accident cars, accident is over
                    if (v->isAccidentTarget || v->isReckless || v->isCrashed) {
                        currentAccident.pending = false; 
                        currentAccident.active = false;
                    }
                    return true;
                }

                // Normal check
                if (v->IsOffScreen() || v->toBeRemoved) {
                    // Double check we aren't deleting a pointer we hold
                    if (v.get() == currentAccident.car1 || v.get() == currentAccident.car2) {
                        currentAccident.car1 = nullptr;
                        currentAccident.car2 = nullptr;
                        currentAccident.pending = false;
                        currentAccident.active = false;
                    }
                    return true;
                }
                return false;
            }), vehiclesBottom.end());


        // --- Pending Accident Logic (The Collision) ---
        if (currentAccident.pending) {
            // FIX: Ensure both cars still exist!
            if (currentAccident.car1 && currentAccident.car2) {
                // Check if they hit
                float dist = currentAccident.car2->GetX() - currentAccident.car1->GetX();
                
                // FIX: Check if touched OR tunnelled (dist became negative slightly)
                // This prevents cars phasing through each other at high speeds
                if (dist < VEHICLE_WIDTH - 10.0f && dist > -VEHICLE_WIDTH) {
                    // CRASH!
                    currentAccident.pending = false;
                    currentAccident.active = true;
                    
                    currentAccident.car1->isCrashed = true;
                    currentAccident.car2->isCrashed = true;
                    currentAccident.car2->isReckless = false; 
                    
                    // Stop them
                    currentAccident.car1->SetMoving(false);
                    currentAccident.car2->SetMoving(false);
                    
                    // Align visually
                    currentAccident.x = currentAccident.car1->GetX() + (VEHICLE_WIDTH/2);
                    currentAccident.y = currentAccident.car1->GetY();

                     for (auto& v : vehiclesBottom) {
                        if (v->IsAmbulance()) {
                            static_cast<Ambulance*>(v.get())->AssignAccident(currentAccident.x, currentAccident.y);
                            v->SetTargetY(currentAccident.y);
                        }
                    }
                }
            } else {
                // One car disappeared? Cancel pending to avoid ghost lock
                currentAccident.pending = false;
                currentAccident.active = false;
            }
        }


        // --- Bottom Road Special Logic ---
        Ambulance* activeAmbulance = nullptr;
        Depannage* activeTow = nullptr;

        for (auto& v : vehiclesBottom) {
            if (v->IsAmbulance()) activeAmbulance = static_cast<Ambulance*>(v.get());
            if (v->IsDepannage()) activeTow = static_cast<Depannage*>(v.get());
        }

        // 1. Tow Truck Logic
        if (activeTow) {
            if (activeTow->hasPickedUp && currentAccident.active) {
                // Attach cars to tow truck
                if (currentAccident.car1) {
                    currentAccident.car1->isTowed = true;
                    currentAccident.car1->isCrashed = false; 
                    currentAccident.car1->isAccidentTarget = false; 
                    currentAccident.car1->towOffsetX = 100.0f; 
                    currentAccident.car1->SetY(activeTow->GetY()); 
                }
                if (currentAccident.car2) {
                    currentAccident.car2->isTowed = true;
                    currentAccident.car2->isCrashed = false;
                    currentAccident.car2->towOffsetX = 200.0f; 
                    currentAccident.car2->SetY(activeTow->GetY());
                }
                currentAccident.active = false; 
            }
        }
        
        // Update Towed Cars Positions
        for (auto& v : vehiclesBottom) {
            if (v->isTowed && activeTow) {
                v->SetX(activeTow->GetX() + v->towOffsetX);
                v->SetY(activeTow->GetY()); 
            }
        }


        // 2. Ambulance Lane Logic
        if (activeAmbulance) {
            if (activeAmbulance->state == TO_HOSPITAL) {
                activeAmbulance->SetTargetY(laneYBottom[2]);
            }
            else if (activeAmbulance->state == TO_ACCIDENT && currentAccident.active) {
                activeAmbulance->SetTargetY(currentAccident.y);
            }
        }

        // 3. General Traffic Loop
        for (size_t i = 0; i < vehiclesBottom.size(); ++i) {
            auto& v = vehiclesBottom[i];

            if (v->isCrashed || v->isTowed) continue; 
            if (v->IsAmbulance() || v->IsDepannage()) { v->Update(); continue; }

            // YIELD LOGIC START
            if (!v->isReckless && !v->laneLock && !v->HasChangedLane()) {
                auto tryYield = [&](Vehicle* emergencyVehicle) {
                    if (emergencyVehicle && emergencyVehicle->IsMoving()) {
                         // Check same lane
                         if (fabs(v->GetTargetY() - emergencyVehicle->GetTargetY()) < 5.0f) {
                             // Check if emergency vehicle is BEHIND and approaching
                             float dist = emergencyVehicle->GetX() - v->GetX();
                             if (dist > 0 && dist < 350.0f) {
                                 // Dodge
                                 int currentLaneIdx = 0;
                                 if (fabs(v->GetY() - laneYBottom[1]) < 5) currentLaneIdx = 1;
                                 if (fabs(v->GetY() - laneYBottom[2]) < 5) currentLaneIdx = 2;
                                 
                                 int targetLane = (currentLaneIdx + 1) % 3;
                                 v->SetTargetY(laneYBottom[targetLane]);
                                 v->SetChangedLane(true);
                             }
                         }
                    }
                };
                tryYield(activeAmbulance);
                tryYield(activeTow);
            }
            // YIELD LOGIC END

            bool stop = false;

            // Collision Check
            if (!v->isReckless) {
                // Accident Avoidance - FIX: Dont let locked cars dodge
                if (currentAccident.active && !v->HasChangedLane() && !v->laneLock) {
                    if (fabs(v->GetY() - currentAccident.y) < 5.0f && v->GetX() > currentAccident.x) {
                        if (v->GetX() - currentAccident.x < 300) {
                            int currentLaneIdx = 0;
                            if (fabs(v->GetY() - laneYBottom[1]) < 5) currentLaneIdx = 1;
                            if (fabs(v->GetY() - laneYBottom[2]) < 5) currentLaneIdx = 2;
                            int targetLane = (currentLaneIdx + 1) % 3;
                            v->SetTargetY(laneYBottom[targetLane]);
                            v->SetChangedLane(true);
                        }
                    }
                }
                
                // Traffic light
                float stopX = lightBottom.GetStopLineX(true);
                if (lightBottom.IsRed() && fabs(v->GetX() - stopX) < 50) stop = true;
                
                // Car Collision
                if (!stop) {
                    for (size_t j = 0; j < vehiclesBottom.size(); ++j) {
                        if (i == j) continue;
                        auto& other = vehiclesBottom[j];
                        if (other->isTowed) continue; 

                        if (fabs(v->GetTargetY() - other->GetTargetY()) < 5.0f) {
                            if (other->GetX() < v->GetX()) { 
                                float frontOfOther = other->GetX() + VEHICLE_WIDTH;
                                if (v->GetX() - frontOfOther < SAFE_DISTANCE) {
                                    stop = true; 
                                    break;
                                }
                            }
                        }
                    }
                }
            } 
            else {
                // Reckless driver logic: Ignore safety
            }
            
            v->SetForcedStop(stop);
            v->Update(stop);
        }

        // Top Road
        for (size_t i = 0; i < vehiclesTop.size(); ++i) {
             auto& v = vehiclesTop[i];
             bool stop = false;
             if (lightTop.IsRed() && fabs(v->GetX() - lightTop.GetStopLineX(false)) < 50) stop = true;
             if (!stop) {
                 for (size_t j = 0; j < vehiclesTop.size(); ++j) {
                     if (i == j) continue;
                     if (vehiclesTop[j]->GetTargetY() == v->GetTargetY() && vehiclesTop[j]->GetX() > v->GetX()) {
                         if (vehiclesTop[j]->GetX() - VEHICLE_WIDTH - v->GetX() < SAFE_DISTANCE) { stop = true; break; }
                     }
                 }
             }
             v->SetForcedStop(stop);
             v->Update(stop);
        }

        ambulanceActive = (activeAmbulance != nullptr);
        if (ambulanceActive) {
            screenAlertTimer += delta;
            if (screenAlertTimer >= 0.5f) {
                screenAlertOn = !screenAlertOn;
                screenAlertTimer = 0.0f;
            }
        } else {
            screenAlertOn = false;
        }
    }

    void Draw() const {
        road.Draw();
        lightTop.Draw();
        lightBottom.Draw();
        DrawTexture(hospitalTexture, 10, ROAD_Y_BOTTOM + ROAD_HEIGHT + 10, WHITE);
        
        for (auto& v : vehiclesTop) v->Draw();
        for (auto& v : vehiclesBottom) v->Draw();

        if (screenAlertOn) {
            DrawRectangle(0, 0, 20, SCREEN_HEIGHT, Fade(RED, 0.7f));
            DrawRectangle(SCREEN_WIDTH - 20, 0, 20, SCREEN_HEIGHT, Fade(RED, 0.7f));
        }
        
        DrawText("Press 'E' for Ambulance", 10, 10, 20, WHITE);
        DrawText("Press 'D' for Tow Truck", 10, 35, 20, WHITE);
        DrawText("Press 'A' for Accident", 10, 60, 20, WHITE);
        
        if(currentAccident.active) DrawText("ACCIDENT ACTIVE!", SCREEN_WIDTH/2 - 100, 50, 20, RED);
        if(currentAccident.pending) DrawText("IMPACT IMMINENT...", SCREEN_WIDTH/2 - 110, 50, 20, ORANGE);
    }

    ~Simulation() {
        UnloadSound(siren);
        UnloadTexture(hospitalTexture);
    }
};

int main() {
    InitAudioDevice();
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Traffic Sim: Accidents & Ambulance");
    SetTargetFPS(60);
    
    {
        Simulation sim;
        sim.Init();
        while (!WindowShouldClose()) {
            float delta = GetFrameTime();
            sim.Update(delta);
            BeginDrawing();
            ClearBackground(SKYBLUE);
            sim.Draw();
            EndDrawing();
            if (IsKeyPressed(KEY_E)) sim.CallAmbulance();
            if (IsKeyPressed(KEY_D)) sim.CallDepannage();
            if (IsKeyPressed(KEY_A)) sim.TriggerRandomAccident();
        }
    } 

    CloseAudioDevice();
    CloseWindow();
    return 0;
}