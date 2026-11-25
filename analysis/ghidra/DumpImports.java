// Diagnostic: what does Ghidra know about the import table?
// args: <hex rva> <count>   -- dump symbols/data at consecutive 8-byte slots
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;

public class DumpImports extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        Address base = currentProgram.getImageBase();
        int ext = 0, extNamed = 0;
        FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
        int n = 0;
        while (it.hasNext() && n < 100000) {
            Function f = it.next();
            n++;
            if (f.isExternal()) { ext++; if (f.getName().toLowerCase().contains("strcpy")) extNamed++; }
        }
        println("functions seen: " + n + ", external: " + ext + ", external strcpy-like: " + extNamed);
        SymbolIterator si = currentProgram.getSymbolTable().getSymbolIterator(true);
        int impSyms = 0;
        String first = "";
        while (si.hasNext()) {
            Symbol s = si.next();
            if (s.getName().startsWith("__imp_")) { impSyms++; if (first.isEmpty()) first = s.getAddress() + " " + s.getName(); }
        }
        println("__imp_ symbols: " + impSyms + "  first: " + first);
        long rva = Long.parseLong(args[0], 16);
        int count = Integer.parseInt(args[1]);
        for (int i = 0; i < count; i++) {
            Address a = base.add(rva + i * 8);
            Data d = getDataAt(a);
            Symbol s = getSymbolAt(a);
            ReferenceIterator ri = currentProgram.getReferenceManager().getReferencesTo(a);
            StringBuilder refs = new StringBuilder();
            while (ri.hasNext()) { Reference r = ri.next(); refs.append(" <- ").append(r.getFromAddress()).append("(").append(r.getReferenceType()).append(")"); if (refs.length() > 300) break; }
            println(String.format("%s val=%016x data=%s sym=%s refs%s", a, getLong(a), d == null ? "-" : d.getDataType() + ":" + d.getDefaultValueRepresentation(),
                s == null ? "-" : s.getName(), refs));
        }
    }
}
