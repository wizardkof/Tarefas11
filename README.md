# Tarefas11

## Requisitos

- Linux com KDE Plasma 6.8 ou superior (Wayland/X11).
- Qt 6.8 ou superior (Core, Concurrent, DBus, Gui, Network, QML, Quick, Quick Controls 2 e Widgets).
- Compilador C++20, CMake 3.21+, Ninja e fish.
- Para integração com o visual do KDE: `qqc2-desktop-style`.
- Opcionais: `hwdata` (identificação PCI), `udisks2` (temperatura S.M.A.R.T. SATA), `vulkan-tools` e `mesa-utils` (informações gráficas), `nvidia-utils` (telemetria NVIDIA).

No Garuda Linux / Arch Linux:

```fish
sudo pacman -S --needed base-devel cmake ninja fish qt6-base qt6-declarative qt6-svg qt6-wayland qqc2-desktop-style hwdata udisks2
```

## Instalação

Clone o repositório e compile:

```fish
mkdir -p ~/Projetos
git clone https://github.com/wizardkof/Tarefas11.git ~/Projetos/Tarefas11
cd ~/Projetos/Tarefas11
fish build.fish
ctest --test-dir build --output-on-failure
sudo cmake --install build --prefix /usr/local
kbuildsycoca6 --noincremental
```

Executar:

```fish
tarefas11
```

Para testar sem instalar:

```fish
./build/tarefas11
```
