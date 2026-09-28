#pragma once
// -----------------------------------------------------------------------------
// Gen2MonitorSync.h
//
// Pulls the network monitors GEN2 has dispatched for this device's license key
// and applies them to the local monitor list. One-way: GEN2 -> device.
//
// Uses GEN2's existing ground-probe job pipeline exactly as it already is —
// no server-side changes:
//
//   GET  <gen2ServerUrl>/api/groundprobe/jobs?license_key=&org_id=
//        Returns a bare JSON array of job rows. Claiming them marks them
//        "delivered", and any delivered-but-unacked job re-surfaces on a
//        later call, which is what makes retrying safe.
//
//   POST <gen2ServerUrl>/api/groundprobe/jobs/<id>/ack
//        Completes the job. On a "remove" ack GEN2 also deletes its own
//        probes row and that monitor's history, so acking is a real state
//        change, not just bookkeeping.
//
// Credentials go in the QUERY STRING for these two endpoints, unlike the
// ingest endpoint which takes them in the body. That asymmetry is GEN2's.
//
// A job is acked only AFTER it has been applied and persisted locally, so a
// failure leaves it unacked and it comes back on the next poll — at-least-once
// delivery rather than fire-and-forget.
//
// Two limitations inherited from GEN2's API, worth knowing before relying on
// this:
//
//   * Jobs are incremental add/remove DELTAS. There is no desired-state
//     snapshot, version or etag, so if a job is never applied the two sides
//     simply differ, with no reconciliation path to detect or repair it.
//
//   * Jobs are scoped per LICENSE KEY, not per device — GEN2 never routes on
//     server_id. It hands each job to exactly one caller (FOR UPDATE SKIP
//     LOCKED), so two probes sharing a license key will steal each other's
//     jobs. Use one license key per device.
// -----------------------------------------------------------------------------

#include <Arduino.h>
#include <ArduinoJson.h>
#include "config/ConfigManager.h"
#include "network/NetworkManager.h"

class Gen2MonitorSync {
public:
    Gen2MonitorSync(ConfigManager &config, NetworkManager &network);

    // Call every loop(); internally rate-limited to gen2SyncIntervalS and a
    // no-op unless gen2SyncEnabled with credentials present.
    void loop();

private:
    ConfigManager &config_;
    NetworkManager &network_;

    unsigned long lastPollMs_ = 0;

    // At most this many jobs are applied per poll, so one large backlog can't
    // monopolise loop() — the remainder simply arrive on the next poll.
    static constexpr uint8_t MAX_JOBS_PER_POLL = 8;

    bool pollOnce();
    // Applies one job row. Sets `handled` false for a job this firmware can't
    // action (unknown type, missing fields) — those are still acked, so GEN2
    // doesn't retry them forever.
    bool applyJob(JsonObjectConst job, bool &handled);
    bool ackJob(const String &jobId);
};
