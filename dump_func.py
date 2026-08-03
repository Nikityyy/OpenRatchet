# dump_func.py
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
import sys

def dump_function_at(addr_str):
    monitor = ConsoleTaskMonitor()
    addr = currentProgram.getAddressFactory().getAddress(addr_str)
    func = getFunctionContaining(addr)
    if not func:
        print("No function found at " + addr_str)
        return
    
    print("Function Name: " + func.getName())
    
    decompInterface = DecompInterface()
    decompInterface.openProgram(currentProgram)
    
    results = decompInterface.decompileFunction(func, 0, monitor)
    if not results.decompileCompleted():
        print("Failed to decompile")
        return
        
    print("--- DECOMPILATION START ---")
    print(results.getDecompiledFunction().getC())
    print("--- DECOMPILATION END ---")
    
    print("--- DISASSEMBLY START ---")
    listing = currentProgram.getListing()
    inst = listing.getInstructionAt(func.getEntryPoint())
    while inst and inst.getAddress().compareTo(func.getBody().getMaxAddress()) <= 0:
        print("{} {}".format(inst.getAddress(), inst))
        inst = inst.getNext()
    print("--- DISASSEMBLY END ---")

dump_function_at("0x20b6d0")
