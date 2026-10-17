#pragma once
// HMI side of the PC link (what HMI_MCU did): polls /api/status for the other boards,
// sends the HMI_MCU heartbeat, forwards commands for the boards that are still separate
// (Robot3Axis, ImageSender, ControlMCU voice), AI approvals, and fetches the last camera JPEG.
// The drive commands no longer go through the PC: the UI calls command_execute() directly.
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

// PC_SOUND is the voice board: ControlMCU (ESP32-S3) since SoundMCU was removed.
enum PcBoard { PC_DRIVE = 0, PC_ROBOT = 1, PC_CAM = 2, PC_SOUND = 3, PC_BOARDS = 4 };

struct PcBoardState {
    bool online;
    int rssi;
    float temp;
    char ip[16];
    char lastSeen[9]; // HH:MM:SS of the last poll that saw it online
};

struct PcStatus {
    bool reachable;          // last /api/status poll succeeded
    uint32_t updatedMs;
    PcBoardState board[PC_BOARDS];
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
};

esp_err_t pc_link_start();
PcStatus pc_status();                         // snapshot copy
bool pc_send_command(PcBoard board, const char *command); // queued, false when the queue is full
void pc_send_approval(const char *id, bool approve);
void pc_request_image();                      // fetch /api/hmi/last_image in the background

// Last decoded camera frame (RGB565, PSRAM). Valid until the next pc_request_image() completes;
// the caller holds the image lock while drawing it.
struct PcImage { const uint8_t *pixels; uint16_t width, height; uint32_t sequence; char error[40]; };
bool pc_image_lock(PcImage *out, uint32_t timeoutMs);
void pc_image_unlock();
