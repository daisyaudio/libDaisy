#pragma once

#include "desktop_devkit.h"

static constexpr size_t kLongPressDuration = 1500;

enum CanvasIds { canvasLedDisplay = 0, NUM_CANVASES };

void FlushLeds(const daisy::UiCanvasDescriptor &canvasDescriptor) {
  auto *hw = reinterpret_cast<daisy::desktop_devkit::Hardware *>(
      canvasDescriptor.handle_);
  hw->UpdateLeds();
}

void ClearLeds(const daisy::UiCanvasDescriptor &canvasDescriptor) {
  auto *hw = reinterpret_cast<daisy::desktop_devkit::Hardware *>(
      canvasDescriptor.handle_);
  hw->ClearLeds();
}

/** Interface class used for generating UI events from Pot inputs. */
class PotListener {
public:
  /** Initialize the Object */
  void Init(daisy::desktop_devkit::Hardware *hw) { hw_ = hw; }

  /** Read the value from the hardware
   *  TODO: update to wrapper if add pot min/max calibration
   */
  float GetPotValue(uint16_t potId) { return hw_->pot[potId].Value(); }

private:
  daisy::desktop_devkit::Hardware *hw_;
};
