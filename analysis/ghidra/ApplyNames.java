// Apply the function table (rva, name) recovered by analysis/analyze.py to the Ghidra program.
// args: <functions.tsv>   (hex rva <TAB> name or "-")
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;

import java.io.BufferedReader;
import java.io.FileInputStream;
import java.io.InputStreamReader;

public class ApplyNames extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        Address base = currentProgram.getImageBase();
        BufferedReader br = new BufferedReader(new InputStreamReader(new FileInputStream(args[0]), "UTF-8"));
        String line;
        int created = 0, named = 0, failed = 0;
        // accepts "rva <TAB> name" or "rva <TAB> module <TAB> name" ("-" = no name)
        while ((line = br.readLine()) != null) {
            if (line.isEmpty() || line.startsWith("#")) continue;
            String[] p = line.split("\t");
            Address a = base.add(Long.parseLong(p[0], 16));
            String name = p.length > 2 ? p[2] : (p.length > 1 ? p[1] : "-");
            if (name.equals("-")) name = null;
            Function f = getFunctionAt(a);
            if (f == null) {
                f = createFunction(a, name != null ? name : null);
                if (f != null) created++;
            }
            if (f == null) { failed++; continue; }
            if (name != null) {
                try {
                    f.setName(name, SourceType.USER_DEFINED);
                    named++;
                } catch (Exception e) {
                    try {
                        f.setName(name + "_" + a.toString(), SourceType.USER_DEFINED);
                        named++;
                    } catch (Exception e2) {
                        failed++;
                    }
                }
            }
        }
        br.close();
        println("ApplyNames: created " + created + ", named " + named + ", failed " + failed);
    }
}
