// Seed Cortex-M vector table entries (and optional extra entry points) as Thumb functions.
// Script args: [vector_table_addr] [nvec] [extra_entry ...]
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.lang.*;
import java.math.BigInteger;

public class SeedCortexM extends GhidraScript {
    void thumbFunc(long a) throws Exception {
        a &= ~1L;
        Address ad = toAddr(a);
        if (!currentProgram.getMemory().contains(ad)) return;
        disassemble(ad);
        if (getFunctionAt(ad) == null) createFunction(ad, null);
    }
    public void run() throws Exception {
        String[] a = getScriptArgs();
        long vt = a.length > 0 ? Long.decode(a[0]) : currentProgram.getMinAddress().getOffset();
        int n = a.length > 1 ? Integer.decode(a[1]) : 16;
        Register tm = currentProgram.getRegister("TMode");
        Address lo = currentProgram.getMinAddress(), hi = currentProgram.getMaxAddress();
        currentProgram.getProgramContext().setValue(tm, lo, hi, BigInteger.ONE);
        for (int i = 1; i < n; i++) {
            Address va = toAddr(vt + 4 * i);
            if (!currentProgram.getMemory().contains(va)) break;
            long v = getInt(va) & 0xffffffffL;
            if ((v & 1) == 1 && currentProgram.getMemory().contains(toAddr(v & ~1L))) {
                thumbFunc(v);
                createLabel(toAddr(v & ~1L), "vec_" + i, true);
            }
        }
        for (int i = 2; i < a.length; i++) thumbFunc(Long.decode(a[i]));
    }
}
