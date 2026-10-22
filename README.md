<div align="center">

# CircuitAliveEDA

<img src="./resources/bitmaps_png/icons/icon_kicad.png" alt="CircuitAliveEDA logo" width="160" />

**Design, simulate, and develop electronic circuits in one place.**

*An independent personal project based on [KiCad](https://www.kicad.org/).*

**Veja a versão em Português 🇧🇷 [aqui](/README-PT_BR.md)**

---

## What is CircuitAliveEDA?

CircuitAliveEDA is a **KiCad-based electronics development environment**, with custom modifications focused primarily on interactive simulation.

The idea is to make simulation a natural part of circuit design rather than a separate step. You build a circuit using electronic symbols, start the simulation, and watch the components behave directly on the schematic. You can also interact with certain elements, take measurements, and analyze signals as you work.

Because the project is based on KiCad, it also retains the tools needed for printed circuit board development, including libraries, PCB editing, and generating the files required for manufacturing. I want to preserve as much compatibility as possible with KiCad itself, as well as the libraries and tools created by its community.

**CA is still under development.** Some features are already implemented, others have initial support, and many remain future goals. This is not an official version of KiCad and is not affiliated with the KiCad team.

---

#### **CircuitAliveEDA is a strictly personal project, developed and shaped around what I would like to see in electronics design software: an intuitive, practical, yet professional tool that lets me take my projects from the first experiments all the way to PCB design.**

#### **The project will continue to evolve according to my needs and ideas. That said, if anyone else shares this vision or finds the software useful, they are more than welcome to use it too!**

</div>

## Table of Contents

- [What is CircuitAliveEDA?](#what-is-circuitaliveeda)
- [What already works](#what-already-works)
- [Project screenshots](#project-screenshots)
- [What I still want to develop](#what-i-still-want-to-develop)
- [How it works internally](#how-it-works-internally)
- [Quick specifications](#quick-specifications)
- [Documentation](#documentation)
- [Building and running](#building-and-running)
- [Credits and acknowledgments](#credits-and-acknowledgments)
- [Licensing](#licensing)

---

## What already works

### Simulation directly on the schematic

CA already has an initial implementation of interactive simulation using **ngspice**, with early support for digital circuits through **XSPICE**.

Features developed so far include:

- Controls to start, pause, resume, and stop simulations.
- Integration with the schematic editor.
- Interaction with switches while a simulation is running.
- Visual representation of LED behavior.
- Voltage, current, and power measurements.
- An oscilloscope with cursors, triggering, and CSV export.
- Display of logic states `0`, `1`, `Z`, and `X`.
- Tests with analog and digital circuits.

I want testing a circuit to feel as natural as drawing it: change a connection, press **Play**, and see what happens.

### Component models

An important part of the project is making simulation models easier to use. A good interface is not very helpful if every new component requires a complicated setup process.

CA already includes support and documentation for discrete and digital component models, as well as assigning those models to symbols. I have also started working on a **visual model editor** to make this process more accessible.

The [model catalog](docs/circuitalive/MODEL_CATALOG.md) contains information about `.casim` files, available models, tests, and coverage of the standard library.

### Small editor improvements

Beyond the simulator, I have started adapting some editor interactions to make everyday work faster. These include improvements to continuous symbol placement and the ability to **delete elements by double-right-clicking**.

These may be small details, but they make a difference when you spend a lot of time building and modifying circuits.

## Project screenshots

The images below show CircuitAliveEDA during development:

![CircuitAliveEDA — project screenshot 1](./resources/screenshot/Screenshot_20261017_163020.png)

![CircuitAliveEDA — project screenshot 2](./resources/screenshot/Screenshot_20261017_165517.png)

![CircuitAliveEDA — project screenshot 3](./resources/screenshot/Screenshot_20261017_165613.png)

![CircuitAliveEDA — project screenshot 4](./resources/screenshot/Screenshot_20261017_165649.png)

---

## What I still want to develop

I do not want CA to be limited to small circuits or educational examples. My goal is for it to support larger projects as well, with hundreds or thousands of elements, analog and digital circuits, memories, processors, and other electronic systems.

Some ideas I want to explore include:

- Expanding the library of components that can be simulated without complicated configuration.
- Allowing custom models to be created and reused in a modular way.
- Improving virtual instruments and interaction with circuits during simulation.
- Investigating ways to make better use of multiple CPU cores in complex simulations.
- Maintaining a seamless workflow from an initial idea through schematic testing to PCB manufacturing.

These are **development goals**, not a list of completed features. I prefer to document the direction I want to take and record the results as the work progresses.

---

## How it works internally

CircuitAliveEDA is derived from **KiCad 10.0.7** and is developed primarily in **C++**. Its interface, schematic tools, and PCB tools build on KiCad's infrastructure, while the interactive simulation features are integrated through custom modules.

### Simulation engine

The project uses **ngspice** as its circuit simulation engine and **XSPICE** for digital simulation features. On top of this foundation, I have developed a control layer to manage interactive sessions and connect simulation to the editor's tools.

This makes it possible to control simulation execution, monitor signals, and interact with components without relying solely on plots displayed separately from the schematic.

### Models and libraries

The goal is to retain symbols and libraries from the KiCad ecosystem and associate them with the models needed for simulation. CA's model structure, including `.casim` files, is documented in the repository.

In the long term, I want new models to be added without requiring extensive changes to the application's core.

### Quick specifications

| Item | Specification |
|---|---|
| Base project | KiCad 10.0.7 |
| Primary language | C++ |
| Simulation engines | ngspice / XSPICE |
| Schematic and PCB editing | KiCad infrastructure with CA modifications |
| Simulation models | Symbol associations and `.casim` files |
| Initial development environment | Fedora Linux |
| Windows builds | MSVC build documentation |
| Status | Under development |
| Licensing | Subject to the licenses of the derived code and dependencies, including the GPL |

## Documentation

For anyone interested in understanding the software in greater detail or studying its implementation, I keep more technical documentation in [`docs/circuitalive/`](docs/circuitalive/).

- [Building on Fedora](docs/circuitalive/BUILD_FEDORA.md)
- [Manual releases: RPM, AppImage, DEB, and EXE — together or individually](docs/circuitalive/RELEASE_WORKFLOW.md)
- [Building on Windows / MSVC](docs/circuitalive/BUILD_WINDOWS.md)
- [Interactive simulation architecture](docs/circuitalive/INTERACTIVE_SESSION.md)
- [Editor integration](docs/circuitalive/EDITOR_SIMULATION.md)
- [Measurements and oscilloscope](docs/circuitalive/MEASUREMENTS.md)
- [Analog and digital models](docs/circuitalive/BASIC_AND_DIGITAL_MODELS.md)
- [Visual model editor](docs/circuitalive/MODEL_AUTHOR.md)
- [Model associations](docs/circuitalive/MODEL_FILES.md)
- [Model catalog](docs/circuitalive/MODEL_CATALOG.md)
- [Overall development map](docs/circuitalive/Mapa_KiCad_Simulacao_Interativa.md)

## Building and running

Development started on Fedora, and the repository includes build scripts for that environment. Once the dependencies are installed, the basic process is:

```bash
tools/circuitalive/configure.sh
tools/circuitalive/build.sh
tools/circuitalive/install.sh
```

To launch the application, depending on your installation:

```bash
./circuitalive
```

See the [Fedora](docs/circuitalive/BUILD_FEDORA.md) and [Windows/MSVC](docs/circuitalive/BUILD_WINDOWS.md) guides for complete requirements and instructions. As the project is still under development, builds and features may have limitations or issues.

---

## Credits and acknowledgments

### KiCad

CircuitAliveEDA would not be possible without **[KiCad](https://www.kicad.org/)** and the team and community who have built and maintained such a comprehensive electronics design tool.

The schematic editor, PCB editor, library system, manufacturing tools, and much of CA's underlying infrastructure come from KiCad. **Full credit for that original work belongs to the project's developers and contributors.**

- [KiCad source code](https://gitlab.com/kicad/code/kicad)
- [Developer documentation](https://dev-docs.kicad.org/)

### ngspice and XSPICE

I would also like to thank the developers of **[ngspice](https://ngspice.sourceforge.io/)** and **XSPICE**, whose work provides an essential part of the simulation technology used by CircuitAliveEDA.

### CircuitAliveEDA

The interface adaptations, interactive simulation integration, and other custom extensions are being developed as part of this project.

**Developed by: Vinicius Dias de Jesus — DMEE Technology**

## Licensing

As a derivative of KiCad, CircuitAliveEDA must comply with the licenses of the original code and its dependencies, including the applicable requirements of the **GNU General Public License (GPL)**. Copyright notices and credits to the original authors must be preserved.

CircuitAliveEDA **is not officially affiliated with the KiCad project**.

---

<div align="center">

**CircuitAliveEDA — DMEE Technology**

*A project I started for my own circuits, and one I want to keep developing and sharing.*

</div>
