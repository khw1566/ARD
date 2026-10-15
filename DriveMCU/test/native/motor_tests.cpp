#include <cassert>
#include <cmath>
#include <deque>
#include <functional>
#include <string>
#include <vector>
#include <cstdio>
#include "Arduino.h"
uint32_t testMillis=0;
struct String : std::string { String(unsigned long n): std::string(std::to_string(n)) {} };
struct MotorAction { long stepsX,stepsY,stepsZ; uint32_t generation; char axis; };
struct FakeStepper {
    long position=0, remaining=0; unsigned stops=0;
    std::vector<long> moves;
    long distanceToGo() {return remaining;}
    long currentPosition() {return position;}
    void move(long delta) {moves.push_back(delta);position+=delta;}
    void moveTo(long target) {move(target-position);}
    void setCurrentPosition(long target) {position=target;remaining=0;}
    void stop() {++stops;remaining=0;}
    void run() {}
};
FakeStepper stepperX,stepperY,stepperZ;
std::deque<MotorAction> queue;
void *motorMutex=reinterpret_cast<void*>(1),*commandQueue=reinterpret_cast<void*>(2);
bool locked=false;
bool xSemaphoreTake(void*,int) {assert(!locked);locked=true;return true;}
void xSemaphoreGive(void*) {assert(locked);locked=false;}
bool xQueueReceive(void*,MotorAction *a,int) {if(queue.empty())return false;*a=queue.front();queue.pop_front();return true;}
void xQueueReset(void*) {queue.clear();}
#define portMAX_DELAY 0
#define EN_PIN_X 1
#define EN_PIN_Y 2
#define EN_PIN_Z 3
#define LOW 0
#define HIGH 1
void digitalWrite(int,int) {}
void sendLogAsync(const std::string&) {}
enum {MANUAL_OFF,MANUAL_MENU};
int manualMode=MANUAL_OFF;
bool stopActive=false,usePIDRail=false,tofReady=false,isExecutingMove=false,waitingForDelay=false;
uint32_t motionGeneration=0,interCommandDelay=0;
unsigned long lastMoveDurationMs=0,currentMoveStartTime=0,lastPIDTime=0;
float lastDistance=0,targetRailDist=140,stepsPerMmZ=43.48,pid_p=.3,pid_i=.01,pid_d=5,integralRail=0,lastRailError=0;
template<class T,class L,class H> T constrain(T v,L lo,H hi) {return v<lo?T(lo):v>hi?T(hi):v;}
struct Finished {};
unsigned iterations=0;
std::function<void()> onTick;
void vTaskDelay(int) { assert(!locked); testMillis+=10; ++iterations; if(onTick)onTick(); if(iterations>=30)throw Finished{}; }
void emergencyStop();
#include "motor_functions.inc"
void reset() {
    queue.clear();locked=false;iterations=0;testMillis=0;onTick=nullptr;
    stepperX=FakeStepper();stepperY=FakeStepper();stepperZ=FakeStepper();
    stopActive=usePIDRail=tofReady=isExecutingMove=waitingForDelay=false;
    motionGeneration=interCommandDelay=0;manualMode=MANUAL_OFF;lastPIDTime=0;
}
void run() {try {motorTask(nullptr);}catch(Finished&) {} }
int main() {
    reset(); interCommandDelay=100;
    queue.push_back({100,0,0,0,'x'});
    onTick=[] {if(iterations==1) {
        assert(waitingForDelay);
        emergencyStop();assert(!waitingForDelay && stopActive);
        stopActive=false;queue.push_back({200,0,0,motionGeneration,'x'});
    }};
    run();assert(stepperX.moves.size()==1 && stepperX.moves[0]==200);
    reset();motionGeneration=1;queue.push_back({100,0,0,0,'x'});
    run();assert(stepperX.moves.empty());
    reset();usePIDRail=tofReady=true;lastDistance=141;stepperZ.remaining=5;
    run();assert(stepperZ.stops>0); // The PID dead zone must not try to re-lock motorMutex.
    reset();usePIDRail=tofReady=true;queue.push_back({100,0,0,0,'x'});
    stepperX.remaining=9;emergencyStop();
    assert(!usePIDRail && queue.empty() && stepperX.remaining==0 && stopActive);
    std::puts("Drive motor tests passed: delayed STOP/resume, stale queue, PID mutex, emergency stop");
}
