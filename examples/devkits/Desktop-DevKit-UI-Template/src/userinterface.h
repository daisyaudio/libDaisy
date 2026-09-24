#pragma once
#include "desktop_devkit.h"
#include "main_page.h"
#include "ui_glue.h"

/** User Inteface for the application.
 *
 *  Initializes the mechanisms for updating the LEDs at a regular interval,
 *  and generating events from tactile inputs (i.e. pots, buttons).
 *
 *  CVs/Gates do not typically generate UI events because we dont' want to flood
 * the event fifo since we want to handle those signals as they arrive from the
 * audio callback. It is, however, possible to access the hardware class in the
 * `Draw()` method of any UIPage if visualizing those signals is important; it
 * should be kept in mind that the period that the Draw method is called is
 * often much slower than the rate the inputs actually update.
 *
 */
class Ui
{
  public:
    void Init(daisy::desktop_devkit::Hardware *hw)
    {
        hw_ = hw;

        daisy::UI::SpecialControlIds specialControlIds; //< None
        daisy::UiCanvasDescriptor    ledDisplayDescriptor;
        ledDisplayDescriptor.id_            = canvasLedDisplay;
        ledDisplayDescriptor.handle_        = hw_;
        ledDisplayDescriptor.updateRateMs_  = 16; //< 60Hz
        ledDisplayDescriptor.clearFunction_ = ClearLeds;
        ledDisplayDescriptor.flushFunction_ = FlushLeds;

        pot_listener_.Init(hw_);
        pot_monitor_.Init(event_queue_,
                          pot_listener_,
                          125,
                          (1.f / (1 << 9)),
                          (1.f / (1 << 12)));

        // Init the pages:
        ui_.Init(event_queue_,
                 specialControlIds,
                 {ledDisplayDescriptor},
                 canvasLedDisplay);
        // ui_.OpenPage(splash_page_);
        ui_.OpenPage(main_page_);
    }

    void GenerateEvents()
    {
        // Pot events
        pot_monitor_.Process();

        // Button events
        for(size_t i = 0; i < daisy::desktop_devkit::kNumButtons; i++)
        {
            auto *btn = &hw_->button[i];
            if(btn->RisingEdge())
            {
                event_queue_.AddButtonPressed(i, 1);
            }
            else if(btn->Pressed() && btn->TimeHeldMs() > kLongPressDuration)
            {
                if(!ignore_next_btn_release[i])
                {
                    event_queue_.AddButtonPressed(i, 2);
                    ignore_next_btn_release[i] = true;
                }
            }
            else if(btn->FallingEdge())
            {
                if(ignore_next_btn_release[i])
                {
                    ignore_next_btn_release[i] = false;
                }
                else
                {
                    event_queue_.AddButtonReleased(i);
                }
            }
        }
    }

    void DoEvents() { ui_.Process(); }

  private:
    daisy::desktop_devkit::Hardware *hw_;
    daisy::UI                        ui_;

    MainPage main_page_;

    /** Event Generation */
    bool ignore_next_btn_release[daisy::desktop_devkit::kNumButtons];
    daisy::UiEventQueue event_queue_;
    PotListener         pot_listener_;
    daisy::PotMonitor<PotListener, daisy::desktop_devkit::kNumPots>
        pot_monitor_;
};
