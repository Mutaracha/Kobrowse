#ifndef KOBROWSE_SLEEP_POLICY_H
#define KOBROWSE_SLEEP_POLICY_H
// Restore overlays only after all native views have released the screen.
inline bool sleepRecoveryAllowed(bool available, bool sleeping, bool sleepView, bool pin, bool usb) {
    return available && !sleeping && !sleepView && !pin && !usb;
}
#endif
