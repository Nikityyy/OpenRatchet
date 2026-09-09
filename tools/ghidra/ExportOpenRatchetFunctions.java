// Export the analyzed executable entry map needed by OpenRatchet overlay AOT.
// Headless-safe and intentionally limited to PS2Recomp's four-column CSV.
// @category OpenRatchet

import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.address.AddressSetView;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;
import ghidra.program.model.symbol.SymbolType;

import java.io.BufferedWriter;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

public class ExportOpenRatchetFunctions extends GhidraScript {
    private static final String OPENRATCHET_EXPORTER_PROTOCOL = "openratchet-entry0x-v1";
    private AddressSet overlayExecutable;
    private Set<Long> requiredCallableEntries = new HashSet<>();

    private static final class Record {
        final String name;
        final long start;
        long end;
        long size;
        final boolean syntheticEntry;

        Record(String name, long start, long end, long size, boolean syntheticEntry) {
            this.name = name;
            this.start = start;
            this.end = end;
            this.size = size;
            this.syntheticEntry = syntheticEntry;
        }
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        println("OpenRatchet exporter protocol: " + OPENRATCHET_EXPORTER_PROTOCOL);
        if (args.length < 2) {
            throw new IllegalArgumentException(
                "usage: ExportOpenRatchetFunctions.java <output.csv> <exec-start:exec-end-exclusive> [...] [entry0xADDRESS ...]");
        }

        overlayExecutable = parseExecutableRanges(args);
        requiredCallableEntries = parseRequiredCallableEntries(args);
        Path output = Paths.get(args[0]).toAbsolutePath().normalize();
        if (output.getParent() != null) {
            Files.createDirectories(output.getParent());
        }

        List<Record> functionRecords = collectFunctions();
        List<Record> directJalRecords = collectDirectJalEntries(functionRecords);
        List<Record> knownRecords = new ArrayList<>(functionRecords);
        knownRecords.addAll(directJalRecords);
        List<Record> requiredCallableRecords = collectRequiredCallableEntries(knownRecords);
        knownRecords.addAll(requiredCallableRecords);
        List<Record> callableLabelRecords = collectCallableLabels(knownRecords);
        List<Record> records = new ArrayList<>(knownRecords);
        records.addAll(callableLabelRecords);
        records.sort(Comparator
            .comparingLong((Record record) -> record.start)
            .thenComparingLong(record -> record.end)
            .thenComparing(record -> record.name));

        try (BufferedWriter writer = Files.newBufferedWriter(output, StandardCharsets.UTF_8)) {
            writer.write("Name,Start,End,Size\n");
            for (Record record : records) {
                writer.write(String.format(
                    "%s,0x%08X,0x%08X,%d%n",
                    record.name,
                    record.start,
                    record.end,
                    record.size));
            }
        }

        println("OpenRatchet function map: " + functionRecords.size() +
                " functions + " + directJalRecords.size() +
                " WAD-direct JAL entries + " + requiredCallableRecords.size() +
                " required Retail callback entries + " + callableLabelRecords.size() +
                " callable executable labels -> " + output);
        if (functionRecords.isEmpty()) {
            throw new IllegalStateException("Ghidra analysis produced no executable functions");
        }
    }

    private AddressSet parseExecutableRanges(String[] args) throws Exception {
        AddressSet ranges = new AddressSet();
        Memory memory = currentProgram.getMemory();
        for (int i = 1; i < args.length; ++i) {
            String token = args[i];
            if (token.startsWith("entry0x") || token.startsWith("entry@") ||
                token.startsWith("entry\\@") || token.startsWith("--entry=")) {
                continue;
            }
            // Ghidra 12.1.2 on Windows has been observed normalizing a legacy
            // ``--entry=0x...`` post-script argument into two script arguments.
            // Accept that shape so stale/manual invocations fail honestly on the
            // address value rather than misclassifying ``--entry`` as a range.
            if (token.equals("--entry")) {
                if (i + 1 >= args.length) {
                    throw new IllegalArgumentException("missing address after --entry");
                }
                ++i;
                continue;
            }
            int colon = token.indexOf(':');
            if (colon <= 0 || colon == token.length() - 1) {
                throw new IllegalArgumentException(
                    "invalid executable range '" + token + "', expected start:end-exclusive");
            }
            long startOffset = parseUnsignedAddress(token.substring(0, colon));
            long endOffset = parseUnsignedAddress(token.substring(colon + 1));
            if (startOffset < 0L || endOffset <= startOffset || endOffset > 0x100000000L) {
                throw new IllegalArgumentException("invalid executable range: " + token);
            }

            Address start = currentProgram.getAddressFactory()
                .getDefaultAddressSpace().getAddress(startOffset);
            Address endInclusive = currentProgram.getAddressFactory()
                .getDefaultAddressSpace().getAddress(endOffset - 1L);
            if (start == null || endInclusive == null) {
                throw new IllegalStateException("executable range is outside default address space: " + token);
            }

            // These ranges originate from OpenRatchet's synthetic ELF PT_LOAD headers.
            // Refuse to silently widen them to Ghidra language-defined memory.
            MemoryBlock first = memory.getBlock(start);
            MemoryBlock last = memory.getBlock(endInclusive);
            if (first == null || last == null || !first.isExecute() || !last.isExecute() ||
                !first.isLoaded() || !last.isLoaded()) {
                throw new IllegalStateException("declared executable ELF range is not loaded/executable: " + token);
            }
            try {
                memory.getByte(start);
                memory.getByte(endInclusive);
            } catch (Exception exception) {
                throw new IllegalStateException(
                    "declared executable ELF range is not initialized/readable: " + token, exception);
            }
            ranges.addRange(start, endInclusive);
        }
        if (ranges.isEmpty()) {
            throw new IllegalStateException("no executable ELF ranges were supplied");
        }
        return ranges;
    }

    private Set<Long> parseRequiredCallableEntries(String[] args) {
        Set<Long> entries = new HashSet<>();
        for (int i = 1; i < args.length; ++i) {
            String token = args[i];
            String addressText = null;
            if (token.startsWith("entry0x")) {
                addressText = token.substring("entry".length());
            }
            else if (token.startsWith("entry@")) {
                addressText = token.substring("entry@".length());
            }
            else if (token.startsWith("entry\\@")) {
                // AnalyzeHeadless 12.1.2 on Windows has been observed inserting
                // a literal backslash before '@'.  Accept this only as a legacy
                // compatibility shape; the build driver no longer emits '@'.
                addressText = token.substring("entry\\@".length());
            }
            else if (token.startsWith("--entry=")) {
                addressText = token.substring("--entry=".length());
            }
            else if (token.equals("--entry")) {
                if (i + 1 >= args.length) {
                    throw new IllegalArgumentException("missing address after --entry");
                }
                addressText = args[++i];
            }
            else {
                continue;
            }

            long entry = parseUnsignedAddress(addressText);
            if ((entry & 3L) != 0L || entry < 0L || entry > 0xffffffffL) {
                throw new IllegalArgumentException("invalid required callable entry: " + token);
            }
            Address address = currentProgram.getAddressFactory()
                .getDefaultAddressSpace().getAddress(entry);
            if (!isOverlayExecutableAddress(address)) {
                throw new IllegalStateException(String.format(
                    "required callable entry 0x%08X is outside the declared executable ELF range",
                    entry));
            }
            entries.add(entry);
        }
        return entries;
    }

    private static long parseUnsignedAddress(String text) {
        String value = text.trim();
        if (value.startsWith("0x") || value.startsWith("0X")) {
            value = value.substring(2);
        }
        if (value.isEmpty()) {
            throw new IllegalArgumentException("empty address");
        }
        return Long.parseUnsignedLong(value, 16);
    }

    private List<Record> collectFunctions() {
        List<Record> records = new ArrayList<>();
        FunctionIterator functions = currentProgram.getFunctionManager().getFunctions(true);
        while (functions.hasNext() && !monitor.isCancelled()) {
            Function function = functions.next();
            Address entry = function.getEntryPoint();
            if (!isOverlayExecutableAddress(entry)) {
                continue;
            }

            AddressSetView body = function.getBody();
            if (body == null || body.getNumAddresses() == 0) {
                continue;
            }
            if (!overlayExecutable.contains(body)) {
                throw new IllegalStateException(
                    "Ghidra function body escapes declared executable ELF range: " + function.getName());
            }
            Address max = body.getMaxAddress();
            if (max == null) {
                continue;
            }
            Address endAddress = max.next();
            if (endAddress == null) {
                throw new IllegalStateException(
                    "function reaches address-space end: " + function.getName());
            }

            long start = entry.getOffset();
            long end = endAddress.getOffset();
            validateRange(function.getName(), start, end);
            records.add(new Record(
                sanitizeName(function.getName()),
                start,
                end,
                body.getNumAddresses(),
                false));
        }
        records.sort(Comparator.comparingLong(record -> record.start));
        return records;
    }


    private List<Record> collectDirectJalEntries(List<Record> existingRecords) throws Exception {
        List<Record> entries = new ArrayList<>();
        Set<Long> starts = new HashSet<>();
        for (Record record : existingRecords) {
            starts.add(record.start);
        }

        Memory memory = currentProgram.getMemory();
        for (ghidra.program.model.address.AddressRange range : overlayExecutable) {
            long startOffset = range.getMinAddress().getOffset();
            long endOffset = range.getMaxAddress().getOffset();
            long alignedStart = (startOffset + 3L) & ~3L;
            for (long sourceOffset = alignedStart; sourceOffset + 3L <= endOffset; sourceOffset += 4L) {
                if (monitor.isCancelled()) {
                    return entries;
                }
                Address source = currentProgram.getAddressFactory()
                    .getDefaultAddressSpace().getAddress(sourceOffset);
                int raw;
                try {
                    raw = memory.getInt(source);
                } catch (Exception exception) {
                    throw new IllegalStateException(
                        String.format("cannot read executable word at 0x%08X", sourceOffset),
                        exception);
                }
                if ((raw >>> 26) != 0x03) {
                    continue;
                }

                long targetOffset = ((sourceOffset + 4L) & 0xF0000000L) |
                    ((((long) raw) & 0x03FFFFFFL) << 2);
                Address target = currentProgram.getAddressFactory()
                    .getDefaultAddressSpace().getAddress(targetOffset);
                if (!isOverlayExecutableAddress(target) || !starts.add(targetOffset)) {
                    continue;
                }

                AddressSetView body = null;
                Function containing = currentProgram.getFunctionManager().getFunctionContaining(target);
                if (containing != null) {
                    body = containing.getBody();
                } else {
                    if (currentProgram.getListing().getInstructionAt(target) == null && !disassemble(target)) {
                        throw new IllegalStateException(String.format(
                            "direct Retail JAL target 0x%08X could not be disassembled", targetOffset));
                    }
                    body = CreateFunctionCmd.getFunctionBody(currentProgram, target, monitor);
                }
                if (body == null || body.getNumAddresses() == 0) {
                    throw new IllegalStateException(String.format(
                        "Ghidra could not derive a body for direct Retail JAL target 0x%08X",
                        targetOffset));
                }
                if (!overlayExecutable.contains(body)) {
                    throw new IllegalStateException(String.format(
                        "Ghidra body for direct Retail JAL target 0x%08X escapes declared executable ELF range",
                        targetOffset));
                }
                Address max = body.getMaxAddress();
                Address endAddress = max == null ? null : max.next();
                if (endAddress == null) {
                    throw new IllegalStateException(String.format(
                        "direct Retail JAL target 0x%08X reaches address-space end", targetOffset));
                }
                long end = endAddress.getOffset();
                long size = containing != null ? end - targetOffset : body.getNumAddresses();
                validateRange(String.format("or_jal_%08x", targetOffset), targetOffset, end);
                entries.add(new Record(
                    String.format("or_jal_%08x", targetOffset),
                    targetOffset,
                    end,
                    size,
                    true));
            }
        }
        entries.sort(Comparator.comparingLong(record -> record.start));
        return entries;
    }

    private Record deriveCallableEntry(long targetOffset, String prefix, String description) throws Exception {
        Address target = currentProgram.getAddressFactory()
            .getDefaultAddressSpace().getAddress(targetOffset);
        if (!isOverlayExecutableAddress(target)) {
            throw new IllegalStateException(String.format(
                "%s 0x%08X is outside the declared executable ELF range",
                description, targetOffset));
        }

        Function containing = currentProgram.getFunctionManager().getFunctionContaining(target);
        AddressSetView body;
        if (containing != null) {
            body = containing.getBody();
        } else {
            if (currentProgram.getListing().getInstructionAt(target) == null && !disassemble(target)) {
                throw new IllegalStateException(String.format(
                    "%s 0x%08X could not be disassembled", description, targetOffset));
            }
            body = CreateFunctionCmd.getFunctionBody(currentProgram, target, monitor);
        }
        if (body == null || body.getNumAddresses() == 0) {
            throw new IllegalStateException(String.format(
                "Ghidra could not derive a body for %s 0x%08X", description, targetOffset));
        }
        if (!overlayExecutable.contains(body)) {
            throw new IllegalStateException(String.format(
                "Ghidra body for %s 0x%08X escapes declared executable ELF range",
                description, targetOffset));
        }
        Address max = body.getMaxAddress();
        Address endAddress = max == null ? null : max.next();
        if (endAddress == null) {
            throw new IllegalStateException(String.format(
                "%s 0x%08X reaches address-space end", description, targetOffset));
        }
        long end = endAddress.getOffset();
        long size = containing != null ? end - targetOffset : body.getNumAddresses();
        String name = String.format("%s_%08x", prefix, targetOffset);
        validateRange(name, targetOffset, end);
        return new Record(name, targetOffset, end, size, true);
    }

    private List<Record> collectRequiredCallableEntries(List<Record> existingRecords) throws Exception {
        List<Record> entries = new ArrayList<>();
        Set<Long> starts = new HashSet<>();
        for (Record record : existingRecords) {
            starts.add(record.start);
        }
        List<Long> required = new ArrayList<>(requiredCallableEntries);
        required.sort(Long::compare);
        for (long targetOffset : required) {
            if (monitor.isCancelled()) {
                return entries;
            }
            if (!starts.add(targetOffset)) {
                continue;
            }
            entries.add(deriveCallableEntry(
                targetOffset, "or_required", "required Retail callable entry"));
        }
        return entries;
    }

    private List<Record> collectCallableLabels(List<Record> functionRecords) {
        List<Record> labels = new ArrayList<>();
        Set<Long> starts = new HashSet<>();
        for (Record record : functionRecords) {
            starts.add(record.start);
        }

        SymbolIterator symbols = currentProgram.getSymbolTable().getSymbolIterator(true);
        while (symbols.hasNext() && !monitor.isCancelled()) {
            Symbol symbol = symbols.next();
            if (symbol == null || !symbol.isPrimary() || symbol.getSymbolType() == SymbolType.FUNCTION) {
                continue;
            }
            Address address = symbol.getAddress();
            if (!isCallableOverlayExecutableAddress(address) || !hasCallReference(address)) {
                continue;
            }
            long start = address.getOffset();
            if (starts.add(start)) {
                labels.add(new Record(sanitizeName(symbol.getName()), start, 0L, 0L, true));
            }
        }

        // Ghidra can have a callable instruction without a useful primary label.
        // Walk analyzed instructions as well so PS2Recomp sees every explicit
        // call destination known to Ghidra, matching the preferred upstream
        // Ghidra workflow without depending on PS2Recomp's optional script file.
        InstructionIterator instructions =
            currentProgram.getListing().getInstructions(overlayExecutable, true);
        while (instructions.hasNext() && !monitor.isCancelled()) {
            Instruction instruction = instructions.next();
            Address address = instruction.getAddress();
            if (!hasCallReference(address)) {
                continue;
            }
            long start = address.getOffset();
            if (starts.add(start)) {
                labels.add(new Record(String.format("entry_%08x", start & 0xffffffffL),
                                      start, 0L, 0L, true));
            }
        }

        if (labels.isEmpty()) {
            return labels;
        }

        List<Long> boundaries = new ArrayList<>();
        for (Record record : functionRecords) {
            boundaries.add(record.start);
        }
        for (Record record : labels) {
            boundaries.add(record.start);
        }
        boundaries.sort(Long::compare);

        for (Record label : labels) {
            long end = 0L;
            for (Record function : functionRecords) {
                if (label.start > function.start && label.start < function.end) {
                    end = function.end;
                    break;
                }
            }
            if (end == 0L) {
                Address address = currentProgram.getAddressFactory()
                    .getDefaultAddressSpace().getAddress(label.start);
                ghidra.program.model.address.AddressRange allowedRange =
                    overlayExecutable.getRangeContaining(address);
                if (allowedRange != null) {
                    end = allowedRange.getMaxAddress().getOffset() + 1L;
                }
            }
            for (long boundary : boundaries) {
                if (boundary > label.start && (end == 0L || boundary < end)) {
                    end = boundary;
                    break;
                }
            }
            if (end <= label.start) {
                end = label.start + 4L;
            }
            validateRange(label.name, label.start, end);
            label.end = end;
            label.size = end - label.start;
        }
        labels.sort(Comparator.comparingLong(record -> record.start));
        return labels;
    }

    private boolean isOverlayExecutableAddress(Address address) {
        if (address == null || overlayExecutable == null || !overlayExecutable.contains(address)) {
            return false;
        }
        MemoryBlock block = currentProgram.getMemory().getBlock(address);
        return block != null && block.isExecute() && block.isLoaded();
    }

    private boolean isCallableOverlayExecutableAddress(Address address) {
        return isOverlayExecutableAddress(address) &&
               currentProgram.getListing().getInstructionAt(address) != null;
    }

    private boolean hasCallReference(Address address) {
        ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(address);
        while (refs.hasNext()) {
            Reference ref = refs.next();
            if (ref != null && ref.getReferenceType() != null && ref.getReferenceType().isCall()) {
                return true;
            }
        }
        return false;
    }

    private static void validateRange(String name, long start, long end) {
        if (start < 0L || end <= start || start > 0xffffffffL || end > 0x100000000L) {
            throw new IllegalStateException(
                "entry is outside 32-bit EE address space: " + name);
        }
    }

    private static String sanitizeName(String name) {
        if (name == null || name.isEmpty()) {
            return "FUN_unnamed";
        }
        // PS2Recomp's current CSV reader is deliberately simple and does not
        // implement RFC-4180 quoting. Keep the four-column file unambiguous.
        return name.replace(',', '_').replace('\r', '_').replace('\n', '_');
    }
}
