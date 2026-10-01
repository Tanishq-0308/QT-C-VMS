#pragma once

#include <QList>
#include "core/UsbUtils.hpp"

class QWidget;
class TransferManager;

namespace RecordTargetDialog {

// Asked when Record is pressed. With no USB device connected nothing is asked. Otherwise:
// "this system only" or "this system and USB"; with USB, which device(s) when there are several,
// then a warning for each chosen device that is FAT32 (4 GB per file), short of space (under
// 30 minutes) or busy with a download. Declining a warning cancels the recording.
// Returns false if the recording must not start; `mirrorTo` gets the devices to also save to.
// `bytesPerSecond` is the recording's expected data rate (for the minutes in the warnings).
bool ask(QWidget* parent, TransferManager* transfers, qint64 bytesPerSecond, QList<UsbUtils::Device>* mirrorTo);

// Minutes of recording that fit in `bytes` at `bytesPerSecond`
int minutesFor(qint64 bytes, qint64 bytesPerSecond);

} // namespace RecordTargetDialog
