// -----------------------------------------------------------------------------
// This file is part of vAmiga
//
// Copyright (C) Dirk W. Hoffmann. www.dirkwhoffmann.de
// Licensed under the GNU General Public License v3
//
// See https://www.gnu.org for license information
// -----------------------------------------------------------------------------

import Foundation

class BreakTableView : NSTableView {
    
    @IBOutlet weak var inspector: Inspector!
    
    var cpu = amigaProxy?.cpu
    
    override func awakeFromNib() {
        
        delegate = self
        dataSource = self
        target = self
        
        action = #selector(clickAction(_:))
    }
    
    func refresh(everything: Bool) {
        
        if (everything) {
            cpu = amigaProxy?.cpu
            for (c,f) in ["addr" : fmt24] {
                let columnId = NSUserInterfaceItemIdentifier(rawValue: c)
                if let column = tableColumn(withIdentifier: columnId) {
                    if let cell = column.dataCell as? NSCell {
                        cell.formatter = f
                    }
                }
            }
            
            reloadData()
        }
    }
    
    @IBAction func clickAction(_ sender: NSTableView!) {
        
        let row = sender.clickedRow
        track("\(row)")
    }
}

extension BreakTableView : NSTableViewDataSource {
    
    func numberOfRows(in tableView: NSTableView) -> Int {
        
        return cpu?.numberOfBreakpoints() ?? 0
    }
    
    func tableView(_ tableView: NSTableView, objectValueFor tableColumn: NSTableColumn?, row: Int) -> Any? {
        
        switch tableColumn?.identifier.rawValue {
            
        case "break":
            return "⛔"
           
        case "addr":
            return cpu?.breakpointAddr(row)
            
        case "cond":
            if let cond = cpu?.breakpointCondition(row) {
                return cond != "" ? cond : "e.g.: D0 == $42 && (A0) == D1"
            }
            
        default:
            break
        }
        
        return "???"
    }
}

extension BreakTableView : NSTableViewDelegate {
    
    func tableView(_ tableView: NSTableView, willDisplayCell cell: Any, for tableColumn: NSTableColumn?, row: Int) {
        
        if let cell = cell as? NSTextFieldCell {
            
            switch tableColumn?.identifier.rawValue {
                
            case "addr":
                cell.textColor = .textColor
                
            case "cond":
                cell.textColor =
                    cpu!.hasSyntaxError(row) ? NSColor.systemRed :
                    cpu!.hasCondition(row) ? NSColor.textColor :
                    NSColor.lightGray
                
            default:
                break
            }
        }
    }
    
    func tableView(_ tableView: NSTableView, setObjectValue object: Any?, for tableColumn: NSTableColumn?, row: Int) {
        
        switch tableColumn?.identifier.rawValue {
            
        case "addr":
            if let value = object as? UInt32 {
                if cpu?.setBreakpointAddr(row, addr: value) ?? false { return }
            }
        case "cond":
            if let value = object as? String {
                if cpu?.setBreakpointCondition(row, cond: value) ?? false { return }
            }
        default:
            break
        }
        
        NSSound.beep()
    }
}
