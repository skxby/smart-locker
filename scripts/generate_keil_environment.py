"""Generate explicit MDK5 targets from local template, without editing original SDK."""
import copy
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

sys.dont_write_bytecode = True
from build_firmware import ROOT, SOURCES, INCLUDES, DEFINES


def main():
    tree = ET.parse(ROOT / 'STM32F4xx_HAL_Code/USER/project_demo/keil_project/stm32f4xx_hal.uvprojx')
    targets = tree.getroot().find('Targets')
    template = copy.deepcopy(targets.find('Target'))
    targets.clear()
    for clock in ['hsi16','hse8_168']:
        target = copy.deepcopy(template)
        def put(path, value):
            node = target.find(path)
            if node is None:
                raise ValueError(path)
            node.text = value
        put('TargetName', 'Env_' + clock)
        prefix = 'TargetOption/TargetCommonOption/'
        put(prefix+'Device', 'STM32F407VGTx')
        put(prefix+'PackID', 'Keil.STM32F4xx_DFP.2.13.0')
        put(prefix+'Cpu', 'IRAM(0x20000000,0x20000) IROM(0x08000000,0x100000) CPUTYPE("Cortex-M4") FPU2 CLOCK(' + ('16000000' if clock == 'hsi16' else '168000000') + ') ELITTLE')
        put(prefix+'FlashDriverDll', 'UL2CM3(-S0 -C0 -P0 -FD20000000 -FC1000 -FN1 -FF0STM32F4xx_1024 -FS08000000 -FL0100000 -FP0($$Device:STM32F407VGTx$CMSIS\\Flash\\STM32F4xx_1024.FLM))')
        put(prefix+'RegisterFile', '$$Device:STM32F407VGTx$Drivers\\CMSIS\\Device\\ST\\STM32F4xx\\Include\\stm32f4xx.h')
        put(prefix+'SFDFile', '$$Device:STM32F407VGTx$CMSIS\\SVD\\STM32F40x.svd')
        put(prefix+'OutputDirectory', '..\\..\\build\\keil-env\\'+clock+'\\')
        put(prefix+'OutputName', 'locker_env')
        put(prefix+'ListingPath', '..\\..\\build\\keil-env\\'+clock+'\\')
        cbase = 'TargetOption/TargetArmAds/'
        put(cbase+'Cads/Optim', '3')
        put(cbase+'Cads/SplitLS', '0')
        put(cbase+'Cads/OneElfS', '1')
        put(cbase+'Cads/VariousControls/Define', ','.join(DEFINES + (['ENV_HSE8_168'] if clock=='hse8_168' else [])))
        put(cbase+'Cads/VariousControls/IncludePath', ';'.join('..\\'+v.replace('/','\\') for v in INCLUDES))
        put(cbase+'Aads/VariousControls/Define', '__MICROLIB')
        put(cbase+'LDads/umfTarg', '0')
        put(cbase+'LDads/useFile', '1')
        put(cbase+'LDads/ScatterFile', '.\\stm32f407vg.sct')
        for name in ['IROM','OCR_RVCT4']:
            put(cbase+'ArmAdsMisc/OnChipMemories/'+name+'/Size', '0x100000')
        put('TargetOption/Utilities/Flash1/UpdateFlashBeforeDebugging', '0')
        put('TargetOption/Utilities/Flash2', 'STLink\\ST-LINKIII-KEIL_SWO.dll')
        put('TargetOption/DllOption/TargetDllName', 'STLink\\ST-LINKIII-KEIL_SWO.dll')
        groups = target.find('Groups')
        groups.clear()
        group = ET.SubElement(groups,'Group')
        ET.SubElement(group,'GroupName').text = 'Environment (no cabinet IO)'
        files = ET.SubElement(group,'Files')
        for source in SOURCES + ['third_party/stm32f4/cmsis/startup_stm32f407xx.s']:
            file = ET.SubElement(files,'File')
            ET.SubElement(file,'FileName').text = Path(source).name
            ET.SubElement(file,'FileType').text = '2' if source.endswith('.s') else '1'
            ET.SubElement(file,'FilePath').text = '..\\' + source.replace('/','\\')
        targets.append(target)
    ET.indent(tree, space='  ')
    output = ROOT / 'firmware/keil/locker_environment.uvprojx'
    tree.write(output, encoding='utf-8', xml_declaration=True)
    print('Generated', output)


if __name__ == '__main__':
    main()
