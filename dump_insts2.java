import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.Listing;

public class dump_insts2 extends GhidraScript {
    @Override
    public void run() throws Exception {
        println("--- DISASSEMBLY START ---");
        Listing listing = currentProgram.getListing();
        Address current = currentProgram.getAddressFactory().getAddress("0x20b600");
        
        for (int i = 0; i < 300; i++) {
            Instruction inst = listing.getInstructionAt(current);
            if (inst == null) {
                disassemble(current);
                inst = listing.getInstructionAt(current);
            }
            if (inst != null) {
                println(inst.getAddress().toString() + " " + inst.toString());
                current = inst.getAddress().add(inst.getLength());
            } else {
                current = current.add(4);
            }
        }
        println("--- DISASSEMBLY END ---");
    }
}
