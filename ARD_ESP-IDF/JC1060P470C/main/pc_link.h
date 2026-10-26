#pragma once
// HMI side of the PC link (what HMI_MCU did): polls /api/status for the other boards,
// sends the HMI_MCU heartbeat, forwards commands for the boards that are still separate
// (Robot3Axis, ImageSender, ControlMCU voice), AI approvals, and fetches the last camera JPEG.
// All drive commands go through the PC to the existing independent DriveMCU.
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

// PC_SOUND is the selected voice endpoint: onboard JC1060 or legacy ControlMCU.
enum PcBoard { PC_DRIVE = 0, PC_ROBOT = 1, PC_CAM = 2, PC_SOUND = 3, PC_BOARDS = 4 };

struct PcBoardState {
    bool online;
    int rssi;
    float temp;
    char ip[16];
    char lastSeen[9]; // HH:MM:SS of the last poll that saw it online
};

// PC sorting sequence (/api/sorting/status). Angles in degrees, rail in mm.
// phase names the motion in progress: capture, analyze, rail_out, yaw_out, roll_out,
// settle, roll_back, yaw_back, rail_back, done, error (idle/prepare otherwise).
struct PcSorting {
    bool valid;              // last sorting poll succeeded
    bool running;
    bool needsReset;
    int stepNum, progress;   // 1..5, 0..100
    char step[12], phase[12];
    char object[40], material[12], updated[9];
    float targetYaw, currentYaw, dumpRoll, currentRoll, targetRail, currentRail;
};

struct PcStatus {
    bool reachable;          // last /api/status poll succeeded
    uint32_t updatedMs;
    PcBoardState board[PC_BOARDS];
    int32_t driveX, driveY, driveZ; // PC accepted-command estimates, not encoder feedback
    bool drivePid;
    float driveTemp, driveHumidity, drivePressure, driveTof;
    // DriveMCU `status` as last read by the PC (about every 300 ms, /api/hmi/updates "drive").
    // While driveLive, driveTof and drivePid come from it and the rail fields are valid.
    bool driveLive, driveRailSettled;
    int driveRailTarget;
    char driveTofStatus[16];
    bool driveTofOk, driveBusy, railConfigValid, railPidMode, railGainsValid;
    float railP, railI, railD;
    int railCenter, railPlus, railMinus, railCapture, railMin, railMax;
    // Sorting destination table (rail config "destinations"), order Metal, Glass, Others, Paper, Plastic.
    // destRail: +1 plus, 0 center, -1 minus. destTilt: +1 forward, -1 backward.
    bool destValid;
    int8_t destRail[5], destTilt[5];
    double driveLat, driveLng;
    // Robot3Axis
    float robotX, robotY, robotZ;
    char robotGrab[12];
    int robotSpeed[3];
    // ImageSender
    char camSensor[16], camRes[12], aiModel[40], aiObject[40], aiCategory[32];
    int camQuality;
    // Sound
    char soundMode[16], soundStatus[24];
    // ControlMCU (if still present)
    char controlMode[8];
    // Pending AI action that needs the operator's approval on this screen.
    bool approvalPending;
    char approvalId[40], approvalDesc[96];
    PcSorting sorting;
    bool cameraStream;        // PC-driven live preview is running
};

esp_err_t pc_link_start();
PcStatus pc_status();                         // snapshot copy
bool pc_send_command(PcBoard board, const char *command); // queued, false when the queue is full
// Priority controls remain available even when the general command queue is full.
void pc_drive_arm();
void pc_drive_stop();
void pc_drive_disarm(); // cancel manual input without cancelling automatic sorting
// Touch joystick, resent every 100 ms while held. Arms the drive control by itself when
// needed (no RE-ARM press). The rail PID does not block it; a rail jog turns the PID off. The PC turns it into a DriveMCU velocity
// jog (finite pulses on older DriveMCU firmware). Raw normalized stick coordinates:
// right/rotation, forward, rail. The PC applies one radial deadzone and a nonlinear
// response before mixing axes. speedPct (10..100) scales the full-stick speed; the
// DriveMCU max speed setting stays the upper limit.
bool pc_drive_joystick(float rotation, float forward, float rail, int speedPct, int deadzonePct);
void pc_drive_neutral();
bool pc_drive_is_armed();
void pc_send_approval(const char *id, bool approve);
void pc_request_image();                      // fetch /api/hmi/last_image in the background
                                              // (also automatic whenever the PC receives a new frame)
// Start or cancel the PC sorting sequence (/api/sorting/start, /api/sorting/cancel).
bool pc_sorting_request(bool start);
bool pc_sorting_reset();
// HTTP status of the latest sorting request; `sequence` changes when a new answer arrives.
struct PcRequestResult { uint32_t sequence; int code; bool start; bool reset; };
PcRequestResult pc_sorting_result();
// Dedicated rail controls; mode selection does not start motion. Numeric arguments
// are integer millimetres (target) or signed steps (step). Modes are saved on the PC.
bool pc_rail_request(const char *op, int value = 0);
PcRequestResult pc_rail_result();
bool pc_send_guidance(const char *cue);        // bounded PC TTS announcement for a local action

// Last decoded camera frame (RGB565, PSRAM). Valid until the next pc_request_image() completes;
// the caller holds the image lock while drawing it.
// width/height are the picture; rows are `stride` pixels apart (the decoder pads to 16).
struct PcImage { const uint8_t *pixels; uint16_t width, height, stride; uint32_t sequence; char error[40]; };
bool pc_image_lock(PcImage *out, uint32_t timeoutMs);
void pc_image_unlock();
