// -----------------------------------------------------------------------------
// This file is part of vAmiga
//
// Copyright (C) Dirk W. Hoffmann. www.dirkwhoffmann.de
// Licensed under the GNU General Public License v3
//
// See https://www.gnu.org for license information
// -----------------------------------------------------------------------------

#include "config.h"
#include "Mouse.h"
#include "Amiga.h"
#include "Chrono.h"
#include "IOUtils.h"

namespace vamiga {

Mouse::Mouse(Amiga& ref, ControlPort& pref) : SubComponent(ref), port(pref)
{

}

const char *
Mouse::getDescription() const
{
    return port.isPort1() ? "Mouse1" : "Mouse2";
}

void Mouse::_reset(bool hard)
{
    RESET_SNAPSHOT_ITEMS(hard)
    
    leftButton = false;
    middleButton = false;
    rightButton = false;
    mouseX = 0;
    mouseY = 0;
    oldMouseX = 0;
    oldMouseY = 0;
    targetX = 0;
    targetY = 0;
}

void
Mouse::resetConfig()
{
    assert(isPoweredOff());
    auto &defaults = amiga.defaults;

    std::vector <Option> options = {
        
        OPT_PULLUP_RESISTORS,
        OPT_SHAKE_DETECTION,
        OPT_MOUSE_VELOCITY
    };

    for (auto &option : options) {
        setConfigItem(option, defaults.get(option));
    }
}

i64
Mouse::getConfigItem(Option option) const
{
    switch (option) {

        case OPT_PULLUP_RESISTORS:  return config.pullUpResistors;
        case OPT_SHAKE_DETECTION:   return config.shakeDetection;
        case OPT_MOUSE_VELOCITY:    return config.velocity;

        default:
            fatalError;
    }
}

void
Mouse::setConfigItem(Option option, i64 value)
{
    switch (option) {
            
        case OPT_PULLUP_RESISTORS:
            
            config.pullUpResistors = value;
            return;

        case OPT_SHAKE_DETECTION:
            
            config.shakeDetection = value;
            return;
            
        case OPT_MOUSE_VELOCITY:
            
            if (value < 0 || value > 255) {
                throw VAError(ERROR_OPT_INVARG, "0...255");
            }
            config.velocity = (isize)value;
            updateScalingFactors();
            return;

        default:
            fatalError;
    }
}

void
Mouse::updateScalingFactors()
{
    assert((unsigned long)config.velocity < 256);
    scaleX = scaleY = (double)config.velocity / 100.0;
}

void
Mouse::_dump(Category category, std::ostream& os) const
{
    using namespace util;
    
    if (category == Category::Config) {
        
        os << tab("Pull-up resistors");
        os << bol(config.pullUpResistors) << std::endl;
        os << tab("Shake detection");
        os << bol(config.shakeDetection) << std::endl;
        os << tab("Velocity");
        os << dec(config.velocity) << std::endl;
    }

    if (category == Category::Summary) {

        os << tab("leftButton");
        os << bol(leftButton) << std::endl;
        os << tab("middleButton");
        os << bol(middleButton) << std::endl;
        os << tab("rightButton");
        os << bol(rightButton) << std::endl;
        os << tab("mouseX");
        os << mouseX << std::endl;
        os << tab("mouseY");
        os << mouseY << std::endl;
    }

    if (category == Category::State) {
        
        os << tab("oldMouseX");
        os << oldMouseX << std::endl;
        os << tab("oldMouseY");
        os << oldMouseY << std::endl;
        os << tab("targetX");
        os << targetX << std::endl;
        os << tab("targetY");
        os << targetY << std::endl;
        os << tab("shiftX");
        os << shiftX << std::endl;
        os << tab("shiftY");
        os << shiftY << std::endl;
    }
}

void
Mouse::changePotgo(u16 &potgo) const
{
    u16 maskR = port.isPort1() ? 0x0400 : 0x4000;
    u16 maskM = port.isPort1() ? 0x0100 : 0x1000;

    if (rightButton || HOLD_MOUSE_R) {
        potgo &= ~maskR;
    } else if (config.pullUpResistors) {
        potgo |= maskR;
    }

    if (middleButton || HOLD_MOUSE_M) {
        potgo &= ~maskM;
    } else if (config.pullUpResistors) {
        potgo |= maskM;
    }
}

void
Mouse::changePra(u8 &pra) const
{
    u16 mask = port.isPort1() ? 0x0040 : 0x0080;

    if (leftButton || HOLD_MOUSE_L) {
        pra &= ~mask;
    } else if (config.pullUpResistors) {
        pra |= mask;
    }
}

i64
Mouse::getDeltaX()
{
    execute();

    i64 result = (i16)(mouseX - oldMouseX);
    oldMouseX = mouseX;

    return result;
}

i64
Mouse::getDeltaY()
{
    execute();

    i64 result = (i16)(mouseY - oldMouseY);
    oldMouseY = mouseY;

    return result;
}

u16
Mouse::getXY()
{
    // Update mouseX and mouseY
    execute();
    
    // Assemble the result
    return HI_LO((u16)mouseY & 0xFF, (u16)mouseX & 0xFF);
}

bool
Mouse::detectShakeXY(double x, double y)
{
    if (config.shakeDetection && shakeDetector.isShakingAbs(x)) {
        msgQueue.put(MSG_SHAKING);
        return true;
    }
    return false;
}

bool
Mouse::detectShakeDxDy(double dx, double dy)
{
    if (config.shakeDetection && shakeDetector.isShakingRel(dx)) {
        msgQueue.put(MSG_SHAKING);
        return true;
    }
    return false;
}

void
Mouse::setXY(double x, double y)
{
    debug(PRT_DEBUG, "setXY(%f,%f)\n", x, y);

    targetX = x * scaleX;
    targetY = y * scaleY;
    
    port.setDevice(CPD_MOUSE);
    port.updateMouseXY((i64)targetX, (i64)targetY);
}

void
Mouse::setDxDy(double dx, double dy)
{
    debug(PRT_DEBUG, "setDxDy(%f,%f)\n", dx, dy);
    
    targetX += dx * scaleX;
    targetY += dy * scaleY;
    
    port.setDevice(CPD_MOUSE);
    port.updateMouseXY((i64)targetX, (i64)targetY);
}

void
Mouse::setLeftButton(bool value)
{
    trace(PRT_DEBUG, "setLeftButton(%d)\n", value);
    
    leftButton = value;
    port.setDevice(CPD_MOUSE);
}

void
Mouse::setMiddleButton(bool value)
{
    trace(PRT_DEBUG, "setMiddleButton(%d)\n", value);

    middleButton = value;
    port.setDevice(CPD_MOUSE);
}

void
Mouse::setRightButton(bool value)
{
    trace(PRT_DEBUG, "setRightButton(%d)\n", value);
    
    rightButton = value;
    port.setDevice(CPD_MOUSE);
}

void
Mouse::trigger(GamePadAction event)
{
    assert_enum(GamePadAction, event);

    debug(PRT_DEBUG, "trigger(%s)\n", GamePadActionEnum::key(event));

    switch (event) {

        case PRESS_LEFT: setLeftButton(true); break;
        case RELEASE_LEFT: setLeftButton(false); break;
        case PRESS_MIDDLE: setMiddleButton(true); break;
        case RELEASE_MIDDLE: setMiddleButton(false); break;
        case PRESS_RIGHT: setRightButton(true); break;
        case RELEASE_RIGHT: setRightButton(false); break;
        default: break;
    }
}

void
Mouse::execute()
{
    mouseX = targetX;
    mouseY = targetY;
}

bool
ShakeDetector::isShakingAbs(double newx)
{
    return isShakingRel(newx - x);
}

bool
ShakeDetector::isShakingRel(double dx) {
    
    // Accumulate the travelled distance
    x += dx;
    dxsum += std::abs(dx);
    
    // Check for a direction reversal
    if (dx * dxsign < 0) {

        u64 dt = util::Time::now().asNanoseconds() - lastTurn;
        dxsign = -dxsign;

        // A direction reversal is considered part of a shake, if the
        // previous reversal happened a short while ago.
        if (dt < 400 * 1000 * 1000) {

            // Eliminate jitter by demanding that the mouse has travelled
            // a long enough distance.
            if (dxsum > 400) {
                
                dxturns += 1;
                dxsum = 0;
                
                // Report a shake if the threshold has been reached.
                if (dxturns > 3) {
                    
                    // debug(PRT_DEBUG, "Mouse shake detected\n");
                    lastShake = util::Time::now().asNanoseconds();
                    dxturns = 0;
                    return true;
                }
            }
            
        } else {
            
            // Time out. The user is definitely not shaking the mouse.
            // Let's reset the recorded movement histoy.
            dxturns = 0;
            dxsum = 0;
        }
        
        lastTurn = util::Time::now().asNanoseconds();
    }
    
    return false;
}

void
Mouse::pressAndReleaseLeft(Cycle duration, Cycle delay)
{
    if (port.isPort1()) {
        agnus.scheduleRel <SLOT_MSE1> (delay, MSE_PUSH_LEFT, duration);
    } else {
        agnus.scheduleRel <SLOT_MSE2> (delay, MSE_PUSH_LEFT, duration);
    }
}

void
Mouse::pressAndReleaseRight(Cycle duration, Cycle delay)
{
    if (port.isPort1()) {
        agnus.scheduleRel <SLOT_MSE1> (delay, MSE_PUSH_RIGHT, duration);
    } else {
        agnus.scheduleRel <SLOT_MSE2> (delay, MSE_PUSH_RIGHT, duration);
    }
}

template <EventSlot s> void
Mouse::serviceMouseEvent()
{
    auto id = agnus.id[s];
    auto duration = agnus.data[s];
    
    switch (id) {

        case MSE_PUSH_LEFT:
            
            setLeftButton(true);
            agnus.scheduleRel<s>(duration, MSE_RELEASE_LEFT);
            break;
            
        case MSE_RELEASE_LEFT:
            
            setLeftButton(false);
            agnus.cancel<s>();
            break;

        case MSE_PUSH_MIDDLE:

            setMiddleButton(true);
            agnus.scheduleRel<s>(duration, MSE_RELEASE_MIDDLE);
            break;

        case MSE_RELEASE_MIDDLE:

            setMiddleButton(false);
            agnus.cancel<s>();
            break;

        case MSE_PUSH_RIGHT:
            
            setRightButton(true);
            agnus.scheduleRel<s>(duration, MSE_RELEASE_RIGHT);
            break;
            
        case MSE_RELEASE_RIGHT:
            
            setRightButton(false);
            agnus.cancel<s>();
            break;

        default:
            fatalError;
    }
}

template void Mouse::serviceMouseEvent<SLOT_MSE1>();
template void Mouse::serviceMouseEvent<SLOT_MSE2>();

}
