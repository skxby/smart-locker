"""Generate an independent Keil project for the S2-01 safe application."""
import copy
import os
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

sys.dont_write_bytecode = True
from build_firmware import ROOT, SOURCES, INCLUDES, DEFINES

PROJECT = ROOT / 'firmware/keil/locker_s201.uvprojx'
S201_SOURCES = ['app/main.c', 'board/board.c', 'hal/timebase_tim6.c', *SOURCES[2:]]


def main():
    tree = ET.parse(ROOT / 'firmware/keil/locker_environment.uvprojx')
    targets = tree.getroot().find('Targets')
    templates = list(targets.findall('Target'))
    if len(templates) != 2:
        raise ValueError('Expected two existing environment targets')
    targets.clear()
    for clock, template in zip(('hsi16', 'hse8_168'), templates):
        target = copy.deepcopy(template)
        def put(path, value):
            node = target.find(path)
            if node is None:
                raise ValueError(path)
            node.text = value
        put('TargetName', 'S201_' + clock)
        prefix = 'TargetOption/TargetCommonOption/'
        output = '..\\..\\build\\keil-s201\\' + clock + '\\'
        put(prefix + 'OutputDirectory', output)
        put(prefix + 'OutputName', 'locker_s201')
        put(prefix + 'ListingPath', output)
        cbase = 'TargetOption/TargetArmAds/Cads/VariousControls/'
        put(cbase + 'Define', ','.join(DEFINES + (['ENV_HSE8_168'] if clock == 'hse8_168' else [])))
        includes = INCLUDES + ['board', '../contracts/include']
        put(cbase + 'IncludePath', ';'.join(os.path.relpath(ROOT / 'firmware' / path, PROJECT.parent).replace('/', '\\') for path in includes))
        groups = target.find('Groups')
        groups.clear()
        group = ET.SubElement(groups, 'Group')
        ET.SubElement(group, 'GroupName').text = 'S2-01 safe application'
        files = ET.SubElement(group, 'Files')
        for source in S201_SOURCES + ['third_party/stm32f4/cmsis/startup_stm32f407xx.s']:
            file = ET.SubElement(files, 'File')
            ET.SubElement(file, 'FileName').text = Path(source).name
            ET.SubElement(file, 'FileType').text = '2' if source.endswith('.s') else '1'
            ET.SubElement(file, 'FilePath').text = '..\\' + source.replace('/', '\\')
        targets.append(target)
    ET.indent(tree, space='  ')
    tree.write(PROJECT, encoding='utf-8', xml_declaration=True)
    print('Generated', PROJECT)


if __name__ == '__main__':
    main()
