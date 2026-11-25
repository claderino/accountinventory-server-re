// Decompile every function and write the C pseudo-code into per-module files.
// args: <outputDir> <functions.tsv> [prototypes.h]
//   functions.tsv : hex rva <TAB> module <TAB> name or "-"   (from analysis/analyze.py)
//   prototypes.h  : optional C prototypes (e.g. variadic log functions) applied before decompiling
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.DataType;
import ghidra.program.model.data.FunctionDefinitionDataType;
import ghidra.program.model.data.ParameterDefinitionImpl;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.Parameter;
import ghidra.program.model.symbol.SourceType;
import ghidra.util.task.ConsoleTaskMonitor;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

public class ExportDecomp extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        File outDir = new File(args[0]);
        outDir.mkdirs();
        Address base = currentProgram.getImageBase();

        Map<Long, String[]> map = new HashMap<Long, String[]>();
        BufferedReader br = new BufferedReader(new InputStreamReader(new FileInputStream(args[1]), "UTF-8"));
        String line;
        while ((line = br.readLine()) != null) {
            if (line.isEmpty() || line.startsWith("#")) continue;
            String[] p = line.split("\t");
            map.put(Long.parseLong(p[0], 16), new String[] { p.length > 1 ? p[1] : "unknown", p.length > 2 ? p[2] : "-" });
        }
        br.close();

        // optional prototypes, plain C, one per line:  <hex rva>|<prototype>;   e.g.
        //   2BE80|void LogPrint(void* self, wchar_t* file, int line, wchar_t* fmt, ...);
        if (args.length > 2) {
            ghidra.app.util.cparser.C.CParser cp = new ghidra.app.util.cparser.C.CParser(currentProgram.getDataTypeManager());
            BufferedReader pr = new BufferedReader(new InputStreamReader(new FileInputStream(args[2]), "UTF-8"));
            int applied = 0;
            while ((line = pr.readLine()) != null) {
                if (line.isEmpty() || line.startsWith("#")) continue;
                String[] p = line.split("\\|", 2);
                Function f = getFunctionAt(base.add(Long.parseLong(p[0], 16)));
                if (f == null) { println("proto: no function at " + p[0]); continue; }
                try {
                    DataType dt = cp.parse(p[1].trim());
                    if (dt instanceof ghidra.program.model.data.FunctionDefinition) {
                        ghidra.app.cmd.function.ApplyFunctionSignatureCmd cmd =
                            new ghidra.app.cmd.function.ApplyFunctionSignatureCmd(
                                f.getEntryPoint(), (ghidra.program.model.data.FunctionDefinition) dt, SourceType.USER_DEFINED);
                        if (cmd.applyTo(currentProgram, monitor)) applied++;
                    }
                } catch (Exception ex) {
                    println("proto failed for " + p[0] + ": " + ex.getMessage());
                }
            }
            pr.close();
            println("prototypes applied: " + applied);
        }

        DecompInterface ifc = new DecompInterface();
        DecompileOptions opts = new DecompileOptions();
        ifc.setOptions(opts);
        ifc.setSimplificationStyle("decompile");
        ifc.openProgram(currentProgram);

        Map<String, PrintWriter> writers = new HashMap<String, PrintWriter>();
        TreeMap<String, List<Function>> byModule = new TreeMap<String, List<Function>>();
        FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
        int total = 0, failed = 0;
        while (it.hasNext()) {
            Function f = it.next();
            if (f.isExternal() || f.isThunk()) continue;
            long rva = f.getEntryPoint().getOffset() - base.getOffset();
            String[] info = map.get(rva);
            String module = info != null ? info[0] : "unknown";
            List<Function> l = byModule.get(module);
            if (l == null) byModule.put(module, l = new ArrayList<Function>());
            l.add(f);
        }
        for (Map.Entry<String, List<Function>> e : byModule.entrySet()) {
            String module = e.getKey().replaceAll("[^A-Za-z0-9_.-]", "_");
            PrintWriter w = new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(outDir, module + ".c")), StandardCharsets.UTF_8));
            writers.put(module, w);
            w.println("// #### module: " + e.getKey() + "   (" + e.getValue().size() + " functions) ####");
            for (Function f : e.getValue()) {
                long rva = f.getEntryPoint().getOffset() - base.getOffset();
                String[] info = map.get(rva);
                long size = f.getBody().getNumAddresses();
                w.println();
                w.println("// ===== " + String.format("sub_%05X", rva) + "  " + f.getName() + "  size=" + size +
                        (f.hasVarArgs() ? "  [varargs]" : "") + " =====");
                total++;
                DecompileResults res = ifc.decompileFunction(f, 120, new ConsoleTaskMonitor());
                if (res != null && res.decompileCompleted()) {
                    w.println(res.getDecompiledFunction().getC());
                } else {
                    failed++;
                    w.println("// DECOMPILE FAILED: " + (res != null ? res.getErrorMessage() : "null"));
                }
                if (total % 100 == 0) { println("decompiled " + total); }
            }
            w.close();
        }
        ifc.dispose();
        println("ExportDecomp: " + total + " functions, " + failed + " failed, " + byModule.size() + " modules");
    }
}
