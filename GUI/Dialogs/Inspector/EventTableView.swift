// -----------------------------------------------------------------------------
// This file is part of vAmiga
//
// Copyright (C) Dirk W. Hoffmann. www.dirkwhoffmann.de
// Licensed under the GNU General Public License v3
//
// See https://www.gnu.org for license information
// -----------------------------------------------------------------------------

@MainActor
class EventTableView: NSTableView {

    @IBOutlet weak var inspector: Inspector!

    var slotInfo = [EventSlotInfo?](repeating: nil, count: EventSlotEnum.count())

    override init(frame frameRect: NSRect) { super.init(frame: frameRect); commonInit() }
    required init?(coder: NSCoder) { super.init(coder: coder); commonInit() }
    
    func commonInit() {

        delegate = self
        dataSource = self
        target = self
    }
    
    private func cache() {
        for row in 0 ..< EventSlotEnum.count() {
            slotInfo[row] = inspector.emu.agnus.cachedSlotInfo(row)
        }
    }

    func refresh(count: Int = 0, full: Bool = false) {

        cache()
        reloadData()
    }
}

extension EventTableView: NSTableViewDataSource {
    
    func numberOfRows(in tableView: NSTableView) -> Int {

        return EventSlotEnum.count()
    }
    
    func tableView(_ tableView: NSTableView, objectValueFor tableColumn: NSTableColumn?, row: Int) -> Any? {
        
        guard let info = slotInfo[row] else { return nil }

        let willTrigger = (info.trigger != INT64_MAX)
        
        switch tableColumn?.identifier.rawValue {

        case "slot":
            return info.slot.description
        case "event":
            return String(cString: info.eventName)
        case "trigger":
            if willTrigger {
                return String(format: "%lld", info.trigger)
            } else {
                return "none"
            }
        case "frame":
            if !willTrigger {
                return ""
            } else if info.frameRel < 0 {
                return "previous"
            } else if info.frameRel == 0 {
                return "current"
            } else {
                return "current + \(info.frameRel)"
            }
        case "vpos":
            if willTrigger {
                return String(format: inspector.hex ? "%x" : "%d", info.vpos)
            } else {
                return ""
            }
        case "hpos":
            if willTrigger {
                return String(format: inspector.hex ? "%x" : "%d", info.hpos)
            } else {
                return ""
            }
        case "remark":
            if !willTrigger {
                return ""
            } else if info.triggerRel == 0 {
                return "due immediately"
            } else {
                return "due in \(info.triggerRel / 8) DMA cycles"
            }
        default:        return "???"
        }
    }
}

extension EventTableView: NSTableViewDelegate {
    
    func tableView(_ tableView: NSTableView, willDisplayCell cell: Any, for tableColumn: NSTableColumn?, row: Int) {
        
        if let cell = cell as? NSTextFieldCell {
            if tableColumn?.identifier.rawValue != "slot" {
                if slotInfo[row]?.trigger == INT64_MAX {
                    cell.textColor = .secondaryLabelColor
                    return
                }
            }
            
            cell.textColor = .textColor
        }
    }
}
