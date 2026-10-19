#!/usr/bin/env fish
# Diagnóstico somente leitura para identificar GPUs/discos no Garuda/Plasma.
# Não precisa de sudo.
echo '=== GPUs DRM (PCI) ==='
for card in (find /sys/class/drm -maxdepth 1 -name 'card[0-9]*' 2>/dev/null | sort)
    set -l dev "$card/device"
    test -d "$dev"; or continue
    echo $card
    for f in vendor device uevent product_name
        if test -r "$dev/$f"
            echo "[$f]"
            head -c 400 "$dev/$f"
            echo
        end
    end
end
if type -q lspci
    echo '=== lspci GPUs ==='
    lspci -nn | grep -Ei 'VGA|3D controller|Display controller'
end
if type -q nvidia-smi
    echo '=== NVIDIA NVML ==='
    nvidia-smi --query-gpu=name,pci.bus_id,driver_version --format=csv,noheader
end
echo '=== Bancos de IDs PCI ==='
for db in /usr/share/hwdata/pci.ids /usr/share/misc/pci.ids /usr/share/pci.ids
    if test -r "$db"
        echo $db
    end
end
