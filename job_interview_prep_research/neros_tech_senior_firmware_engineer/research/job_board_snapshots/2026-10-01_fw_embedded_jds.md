# Neros firmware/embedded 관련 공고 원문 (2026-10-01 Greenhouse API 수집)


## Senior Firmware Engineer, Platform — Torrance, California, United States — id 5195308007 — updated 2026-09-30

https://job-boards.greenhouse.io/nerostechnologies/jobs/5195308007

What you will be doing
Neros builds first-person-view drones for the modern battlefield. As a Senior Firmware Engineer on the Platform team, you will own the common runtime, libraries, SDK, and build system that our flight, ground-station, and autonomy software teams build on — across microcontroller and Linux targets alike. Your customers are other engineers, and the hardest problems in this space are the ones that surface where their code meets the platform.
Responsibilities
- Own the common runtime and libraries every firmware team consumes — logging, telemetry, IPC, configuration — on MCU-based systems and Linux targets
- Own the build system and cross-compile toolchain: fast, reproducible, hermetic builds across every target we ship, and the SDK other teams consume
- Design and maintain the interfaces other teams build on, keeping them stable as the platform evolves underneath
- Solve the hard, ambiguous problems that surface where the platform meets its consumer teams
- Drive resource trade-offs — compute, memory, bandwidth — when subsystems compete for the same budget
- Raise the firmware engineering bar through patterns, code review, and mentoring
You should have the following
- Mastery of embedded C on shipped products, with fluency across both MCU/RTOS and embedded Linux environments
- Have built platform code that other engineers depended on — libraries, SDKs, or protocols consumed by multiple teams — and kept them stable through change
- Hands-on ownership of an embedded build system (e.g. Bazel) — cross-compilation across multiple targets, dependency management, and build reproducibility
- Track record of owning problems that spanned team or subsystem boundaries and driving them to shipped resolution
- Strong resource-constrained design judgment — compute, memory, and bandwidth trade-offs on real hardware
- Debugging mastery — a history of cracking the bugs others gave up on
- Hands-on: you still ship code, and your technical influence comes from the work
Nice to have
- Bazel specifically, especially for embedded/cross-compiled targets — or migrating a firmware org onto it
- Embedded Linux platform internals — kernel, BSPs, Yocto/Buildroot — alongside MCU work
- Flight-stack experience — Betaflight, PX4, or similar
- DSP, comms, or RF-adjacent firmware
- Visible open-source systems work
- You fly — FPV, RC, or drones generally
US Salary Range
$195,000 – $273,000 USD

## Firmware Test Engineer — Torrance, California, United States — id 4941340007 — updated 2026-09-30

https://job-boards.greenhouse.io/nerostechnologies/jobs/4941340007

What you will be doing
- Automated Test Development: Design, develop, and maintain test suites to validate the Neros drone & ground control software
- Test Framework Development: Build and enhance automated testing frameworks and tools that facilitate automated testing
- CI/CD Integration: Integrate automated tests into CI/CD pipelines to enable continuous testing of software
- Ensure Build Stability: Monitor the test results and ensure the stability of builds before releases
- Quality Assurance: Contribute to maintaining high-quality software by ensuring comprehensive test coverage, and enforcing testing best practices
- Documentation: Create and maintain documentation related to automated test cases, test plans, and test results
You should have the following
- 5+ years of software testing with a focus on embedded systems and HIL testing
- Hands-on experience building, setting up HIL test systems
- Strong development skills with a scripting language (e.g. Python) for test automation
- Familiarity with embedded communication protocols - - e.g. I2C, SPI, UART, Ethernet
- Experience with a CI/CD tools - Gitlab CI, Jenkins
- Experience using Git including development workflows
- Ability to thrive in a fast-paced work environment
Nice to have
- Experience with C/C++ is a plus
- Experience with Bazel, Cmake, make or another embedded build system
- Experience testing RF products
- Experience with FPV Drone software including Betaflight, ExpressLRS, PX4, Ardupilot, etc.
US Salary Range
$145,500 - $204,000 USD

## Firmware Engineer - Flight Software — Torrance, California, United States — id 5195301007 — updated 2026-09-30

https://job-boards.greenhouse.io/nerostechnologies/jobs/5195301007

What you will be doing
Neros builds first-person-view drones for the modern battlefield. As a Firmware Engineer on the Flight Software team, you will build the flight-critical firmware that flies the aircraft — flight modes, control algorithms, and the vehicle-side integrations they depend on — across our entire range of products. Your work goes from code to bench to flight test, and what you ship flies in the field.
Responsibilities
- Implement flight modes and control features — position and altitude hold, and the safety and degradation behavior around them
- Port and tune sensors and control algorithms across flight stacks (Betaflight, PX4), preserving flight feel that expert pilots depend on
- Build vehicle-side datalink and payload integrations for new platform variants
- Instrument the vehicle, run bench and flight-test iterations, and debug issues that only reproduce in flight
- Write the unit and integration tests for your firmware, partnering with our Test organization on SITL/HITL and flight-test execution
You should have the following
- Strong embedded C on resource-constrained microcontrollers — you have shipped firmware that runs on real hardware in the field
- Hands-on experience with a flight stack — PX4, Betaflight, or similar — on vehicles that actually flew
- Working grasp of flight-control fundamentals: control loops, filtering, and how sensors (IMU, baro, GPS) become state estimates
- Track record of debugging hardware-coupled problems — failures that only reproduce on the bench or in flight, not in a debugger
- Able to own a problem end to end with light direction — from ambiguous goal to flying firmware
- Test discipline appropriate to safety-relevant firmware
Nice to have
- Datalink, tunneling, or networking-protocol work on embedded systems
- Betaflight internals experience — modes, RC/mixer path, or a maintained fork
- Counter-UAS, radar-guided, or interceptor-adjacent systems exposure or interest
- Estimation depth (EKF/complementary filters, sensor fusion)
- Depth in the PX4 ecosystem — MAVLink, uORB, EKF2, offboard/companion-computer integration, QGroundControl tooling
- You fly — FPV, racing, or fixed-wing RC
US Salary Range
$145,500 – $204,000 USD

## Senior Embedded Linux Engineer — Torrance, California, United States — id 5195277007 — updated 2026-09-30

https://job-boards.greenhouse.io/nerostechnologies/jobs/5195277007

What you will be doing
Neros builds first-person-view drones for the modern battlefield. As Senior Embedded Linux Engineer, you will own the embedded Linux platform underneath Neros products — the distribution, kernel, bootloader, and SDK that our flight, ground-station, and autonomy software teams build on. This is a platform-ownership role: your customers are other engineering teams, and your work is the foundation the whole product line stands on.
Responsibilities
- Own our embedded Linux distribution and board support packages (Yocto/Buildroot) — from board bring-up on new compute platforms to safe, reliable field updates
- Own kernel configuration, device drivers, and the bootloader, including secure boot and update/recovery paths
- Design and maintain the common runtime and libraries other teams consume — logging, telemetry, IPC, configuration — and the cross-compile toolchain and SDK
- Bring up new compute platforms: from first power-on to a production-ready OS image
- Set Linux platform best practices and mentor engineers across teams that build on the platform
You should have the following
- Deep embedded Linux experience — you have built and shipped products on custom Linux images (Yocto or Buildroot), owning the image rather than just using it
- Kernel-level competence: device drivers, device tree, and debugging kernel or boot failures on real hardware
- Bootloader experience (U-Boot or similar), including secure boot concepts and safe update/recovery design
- Strong C, comfortable on both sides of the kernel/user space boundary; scripting (Python/shell) for platform tooling
- Board bring-up experience — taking a new SoC/SoM from schematic review to a booting, stable OS
- Platform-as-product mindset: you have designed libraries, SDKs, or interfaces consumed by other teams — and kept them stable while the platform evolved underneath
Nice to have
- Radio/comms integration on embedded Linux
- Real-time tuning — PREEMPT_RT, latency analysis
- OTA/update systems — Mender, RAUC, SWUpdate, OSTree, or homegrown equivalents
- Upstream contributions — kernel, Yocto/OpenEmbedded, U-Boot
- Rust for systems work
- You fly — FPV, RC, or drones generally
US Salary Range
$165,000 – $231,000 USD

## Flight Software Manager — Torrance, California, United States — id 5164187007 — updated 2026-09-30

https://job-boards.greenhouse.io/nerostechnologies/jobs/5164187007

What you will be doing
Neros is building the flight software that powers our expanding family of drones. As Flight Software Manager, you will lead the team responsible for the mission-critical software running on the aircraft — flight controls, avionics, payload integration, datalink, and onboard health — and the software interfaces our autonomy and ground systems depend on. You will own the reliability, release cadence, and engineering quality of this stack while growing and leading the team that builds it.
Responsibilities
- Lead and grow the Flight Software engineering team, including hiring, mentorship, and performance management
- Own the architecture and delivery of the airside flight-software stack across multiple vehicle platforms
- Drive predictable, safe software releases for mission-critical systems and steward the long-term health of the codebase
- Set and uphold engineering quality practices — code review, testing, and CI/CD — for mission-critical software
- Own the flight software team's test strategy and unit/integration testing, partnering with the Test organization on SITL/HITL and flight-test execution
- Define and maintain the software interfaces consumed by autonomy and adjacent teams
Requirements
- Has led a software team as a hands-on manager — owning both people management and technical direction
- Strong background in embedded, real-time, safety-critical and/or mission-critical software
- Hands-on depth in flight-control / avionics software, or closely adjacent real-time control systems
- Familiarity with flight-stack platforms such as PX4, Betaflight, ArduPilot, or comparable autopilot/flight-control software
- Track record of shipping reliable software on hardware-coupled, schedule-constrained programs
- Strong judgment on test rigor, technical debt, and quality for mission-critical systems
- Able to operate as the interface between the team and senior leadership — managing expectations upward and clearing execution blockers for the team
Nice to have
- Experience with contested-environment datalink / RF comms
- Experience defining platform interfaces consumed by other teams — e.g. autonomy, gcs, etc.
- Multi-vehicle / swarm systems experience
- Background bringing up new vehicle platforms or product lines from prototype to production
US Salary Range
$225,500 - $316,500 USD

## Firmware Engineer - Tennessee Office — Tennessee, United States — id 5095368007 — updated 2026-09-30

https://job-boards.greenhouse.io/nerostechnologies/jobs/5095368007

What you will be doing
Neros is seeking a talented Firmware Engineer to contribute to the design of new peripheral systems including display systems, handsets and ground control systems. This is a hands-on, cross-functional, highly technical role that will play a critical part in the development of new products. You will be a key part of Neros' Peripherals Team.
The Firmware Engineer will take product requirements and directly participate in the development of hardware and software to implement the design. The Firmware Engineer will have the opportunity to support product development from concept, through rapid prototyping and into full scale production.
Key Responsibilities
- Work closely with electrical, mechanical and flight teams to translate mission requirements into production features
- Assist in the development of hardware architectures critical to Neros’ peripheral systems
- Develop, modify and own embedded firmware for Neros peripheral systems including headsets, handsets and future products
- Work with FPGA, MCU and/or embedded Linux systems to implement video conversions, corrections, storage, compression, streaming and other functions
You should have the following
- Bachelor’s or Master’s degree in Electrical or Computer Engineering, Computer Science or a related discipline
- 2+ years of experience
- STM32 family of microcontrollers
- Significant FPGA experience
- Strong C/C++ for embedded systems
- Strong embedded Linux skills
- Strong familiarity with peripherals and interfaces such as SPI, I2C, UARTs, DMA pipelines, ADC/DAC operation, interrupts, etc.
- Comfortable with hardware & software tools - e.g. oscilloscopes, logic analyzers, JTAG/SWD Debuggers, etc.
- Synchronization between clock domains
- Working knowledge of camera interfaces, video formats and compression standards.
- Bare-metal development experience
- Experience using Git including development workflows
- Knowledge or interest in FPV drones or UAVs
- Excellent written and verbal communication skills, with the ability to convey complex technical information to executive stakeholders clearly and effectively.
Nice to have
- Knowledge of High speed data streaming between MCU and FPGA
- FPV drone pilot license Part 107
- Soldering, 3D printing, prototyping
- RTOS experience such as FreeRTOS, Zephyr, etc.
- Gstreamer or ffmpeg video pipeline experience
- Specific experience handling video conversions in FPGAs, SoCs or other platforms
US Salary Range
$138,500 - $194,000 USD

## Senior Firmware Engineer — Ukraine Office — id 5086180007 — updated 2026-09-30

https://job-boards.greenhouse.io/nerostechnologies/jobs/5086180007

What you will be doing
As an early Neros employee, you will get to help decide the direction of our future. We are looking for someone who can write the embedded software on the Neros Archer platform including flight control and radio link code. You will be developing embedded firmware for microcontrollers used within FPV Drones and their support electronics, debugging hardware & firmware problems, supporting requirements development including review, feasibility, and architecture and supporting testing including creating test cases, reviewing test plans and executing tests.
You should have the following
- BS or MS in CS/CE/EE or equivalent industry experience
- Strong C development skills for embedded applications
- Hands-on work with embedded communication protocols - e.g. I2C, SPI, UART, Ethernet
- Experience developing in bare-metal or RTOS firmware environments
- Experience integrating sensors and other external components
- Comfortable with hardware & software tools - e.g. oscilloscopes, logic analyzer, JTAG/SWD Debuggers, etc…
- Experience using Git including development workflows
- Ability to thrive in a fast-paced work environment
Nice to haves
- Experience with ARM Cortex-M processors - e.g. STM32
- Experience with FPV Drone software including Betaflight, ExpressLRS, PX4, Ardupilot, etc.

## Principal Connectivity Engineer — Torrance, California, United States — id 5253713007 — updated 2026-10-01

https://job-boards.greenhouse.io/nerostechnologies/jobs/5253713007

What you will be doing
As a Principal Connectivity Systems Engineer, you will define the technical future of Neros connectivity systems. You will own the architecture that connects our FPV drones, ground stations, repeaters, radios, antennas, embedded systems, video links, command-and-control links, and platform-level electrical constraints into a coherent system that works in the field. This is a principal-level architecture role for someone who can move between communications theory, RF hardware, embedded systems, electrical design, platform integration, test strategy, and product requirements without losing sight of what can be built at startup speed.
Responsibilities
- Own end-to-end connectivity system architecture for current and future FPV drone platforms, ground systems, and supporting infrastructure
- Translate long-term platform requirements into wireless system architectures, development roadmaps, technical proposals, and executable engineering plans
- Make architecture-level tradeoffs across RF performance, link budget, waveform/modulation, antennas, embedded compute, power, thermal, latency, reliability, manufacturability, and schedule
- Define the future of Neros video, command-and-control, telemetry, repeater, mesh, point-to-point, and ground-to-air connectivity capabilities
- Serve as the connectivity team's technical authority in cross-disciplinary architecture reviews, platform decisions, proposal reviews, and feasibility assessments
- Partner with RF design, electrical, embedded software, systems, mechanical, manufacturing, test, product, and leadership teams to turn ambiguous requirements into working systems
- Identify technical risks, infeasible approaches, and missing requirements early enough to change direction quickly, and mentor principal, senior, and staff engineers
You should have the following
- Masters of Science in Electrical Engineering, Electrical and Computer Engineering, RF/Microwave Engineering, Computer Engineering, or a related technical field
- 12+ years of experience designing, architecting, integrating, or leading complex wireless, RF, embedded, electrical, or communications systems
- Deep understanding of wireless communication systems: link budgets, modulation, coding, receiver sensitivity, interference, coexistence, propagation, latency, throughput, reliability, and system-level RF tradeoffs
- Experience architecting systems that combine RF hardware, antennas, embedded software, electrical interfaces, power constraints, and real-world operating requirements
- Demonstrated ability to lead ambiguous, architecture-level technical decisions across multiple engineering disciplines
- Strong electrical engineering fundamentals: power, signal integrity, digital interfaces, mixed-signal systems, sensors, compute, and embedded hardware constraints
- Strong embedded systems judgment: firmware/software architecture, hardware-software interfaces, real-time constraints, telemetry, diagnostics, and field-update considerations
- Experience taking wireless or embedded hardware systems from concept through prototype, validation, field test, production readiness, or deployment
- Ability to write clear technical proposals, architecture documents, trade studies, and development plans for technical and executive audiences
- Track record of mentoring senior technical contributors and influencing engineering direction without relying only on organizational authority
Nice to have
- Drone or UAV connectivity: Experience architecting wireless systems for drones, FPV platforms, robotics, aircraft, or mobile ground systems
- Video and C2 links: Experience with low-latency video links, command-and-control links, telemetry links, mesh radios, point-to-point radios, or contested RF environments
- Full-stack radio systems: Experience spanning RF front ends, antennas, digital baseband, embedded software, networking, and system-level performance
- Fielded hardware: Experience shipping or deploying wireless hardware that had to work outside the lab in harsh, mobile, or high-consequence environments
- Principal-level technical leadership / defense context: Setting architecture for multiple teams, mentoring principal engineers, or acting as final technical reviewer; EW-aware communications, resilient links, tactical radios, or high-reliability systems
US Salary Range
$255,000 – $357,000 USD

## Autonomy Platform & Runtime Lead — Torrance, California, United States — id 5234134007 — updated 2026-09-30

https://job-boards.greenhouse.io/nerostechnologies/jobs/5234134007

What you will be doing
The Autonomy Platform Lead owns the onboard software platform on which all Neros autonomy software runs, spanning the runtime architecture and module interfaces, real time scheduling and latency accounting, on vehicle logging and time synchronization, and the build, release and deployment path onto the aircraft. This role leads a small platform engineering team while contributing directly to the software, and is accountable for maintaining a single configurable runtime across all Neros unmanned aircraft rather than per program forks. The Autonomy Platform Lead additionally represents autonomy in hardware co-design, carrying the compute, power, thermal and mass requirements of the autonomy payload into bill of materials decisions, and owns deployment of the autonomy software stack onto the selected embedded compute.
Responsibilities
- Own the architecture, roadmap and delivery of the autonomy runtime, including the module interface contract, process and thread model, message passing, execution rates and latency guarantees that every other autonomy group develops against
- Maintain a single autonomy runtime across all vehicle platforms through a platform configuration layer, such that a new airframe revision is delivered as a configuration change rather than a software fork, and enforce that boundary against per program divergence
- Own real time scheduling, latency accounting and deadline monitoring, including end to end sensor to actuator measurement on flight hardware under sustained thermal load, and deliver the instrumentation required for latency to be enforced as a release gate
- Own build, release, versioning and deployment of autonomy software to bench and field vehicles, the autonomy continuous integration infrastructure jointly with the evaluation organization, and on vehicle logging and hardware time-stamping, including full rate capture with triggered dump, time synchronization across sensor, compute and flight controller, and log integrity, without degrading control loop performance
- Own the representation of autonomy in hardware co-design and bill of materials decisions by quantifying compute, power, thermal and mass requirements against measured latency budgets, and own deployment of learned components to the selected embedded accelerator, including quantization and characterization of the resulting accuracy and latency trade off
- Lead and grow the platform engineering team through hiring, mentorship and technical review, while continuing to contribute directly to the codebase as an engineer
You should have the following
- 5+ years of professional experience developing production embedded, robotics or real time software, including 2+ years leading an engineering team or a technical program as a tech lead or engineering manager, with continued hands on technical contribution throughout
- Demonstrated delivery of software onto a physical vehicle or embedded platform operating outside a laboratory, such as unmanned aircraft, ground or aerial robotics, autonomous vehicles, automotive or aerospace systems, including responsibility through bring up, integration and field operation rather than simulation or research alone
- Expert proficiency in modern C++ on embedded Linux, and working proficiency in Python for tooling and automation, developed under real time and resource constrained requirements
- Ownership of the architecture of a robotics or vehicle software runtime or middleware layer, covering the process and thread model, inter process communication and message passing, module interface definition, and execution scheduling, using ROS 2, DDS, Zenoh, LCM, or a proprietary equivalent
- Depth in real time systems engineering, including scheduling policies and priorities, CPU affinity and isolation, lock free or zero copy data paths, jitter and deadline analysis, and measurement of end to end latency distributions on target hardware rather than in simulation
- Experience implementing time synchronization and hardware time-stamping across sensors, compute and a flight or vehicle controller, using PTP or gPTP, PPS, sensor trigger lines, or equivalent, and reconciling multiple clock domains in logged data
- Ownership of build, release and continuous integration for embedded targets, including cross compilation and toolchain management, Yocto, Buildroot or containerized build environments, versioned and reproducible artifacts, and over the air or field software update
- Experience building high rate on vehicle data logging, including ring buffer and triggered capture, multi sensor and video streams, storage and bandwidth budgeting, and log integrity, delivered without degrading control loop timing
- Experience deploying neural networks to embedded accelerators such as NVIDIA Jetson and TensorRT, Qualcomm QNN or SNPE, Texas Instruments TIDL, Hailo, or equivalent, including quantization and characterization of the resulting accuracy and latency trade off
- Experience working directly with hardware and electrical engineering teams to specify compute, memory, power, thermal and mass requirements for an embedded compute payload, including system on module or carrier board selection and participation in bill of materials decisions
Nice to Have
- Unmanned aircraft experience specifically, including integration between an onboard mission computer and a flight controller or autopilot, using PX4, ArduPilot and MAVLink or a proprietary equivalent, and familiarity with the flight modes, arming logic and failsafe behavior at that boundary
- Design and build of hardware in the loop benches, including sensor simulation and injection, hardware timestamp instrumentation, and integration of a HIL bench into automated release gating
- Experience carrying a software platform through transition to volume manufacturing, including per unit calibration data management, provisioning at the factory, and staged software update across a large deployed fleet
US Salary Range
$163,500 – $228,500 USD

## Lead, Platform Software — Torrance, California, United States — id 5244552007 — updated 2026-09-30

https://job-boards.greenhouse.io/nerostechnologies/jobs/5244552007

What you will be doing
Neros builds the embedded software that powers our drones and ground systems. As a Lead, Platform Software Engineer, you will build and lead the team that owns our foundational embedded platform — the Linux distribution, kernel, bootloader, common runtime, and SDK that our flight, ground-station, and autonomy software teams build on. You will own the platform's roadmap, reliability, and engineering quality while growing and leading the team behind it and treating every other engineering team as your customer.
Responsibilities
- Lead and grow the Platform Software engineering team, including hiring, mentorship, and performance management
- Own the roadmap and delivery of the embedded platform — Linux distribution and BSPs, kernel and bootloader, common runtime and libraries, and the cross-compile toolchain and SDK
- Run the platform as a product: keep the interfaces our flight, ground, and autonomy teams depend on stable, versioned, and evolving without breaking them
- Make the hard architecture calls — build vs. adopt, interface design, and resource allocation across subsystems
- Set and uphold engineering quality practices — code review, testing, and CI/CD — for foundational software
- Stay hands-on: review platform code and own architecture, not just process
You should have the following
- Has led a software team as a hands-on manager — owning both people management and technical direction
- Deep embedded-platform background: custom Linux (Yocto/Buildroot), kernel/BSPs, bootloaders, and the runtime/SDK layer other teams build on
- Track record of running a platform or foundational-software team as a product — stable interfaces, versioning, and consumer teams who could depend on it
- Strong judgment on build-vs-adopt, interface design, and resource trade-offs across an embedded system
- Able to operate as the interface between the team and senior leadership — managing expectations upward and clearing execution blockers
- Player-coach: still technically credible enough to review BSP/kernel/runtime code and make architecture calls
Nice to Have
- Experience building an embedded platform team from a small nucleus
- MCU/RTOS platform work alongside embedded Linux
- Bazel or comparable build-system leadership across cross-compiled targets
- Real-time, safety-relevant, or mission-critical embedded platforms
- Background bringing up new computer platforms from prototype to production
US Salary Range
$198,000 - $277,000 USD

## Senior Platform Engineer — Torrance, California, United States — id 5241249007 — updated 2026-10-01

https://job-boards.greenhouse.io/nerostechnologies/jobs/5241249007

What you will be doing
The Senior Platform Engineer builds the onboard software platform on which all Neros autonomy software runs. Working within the Autonomy Platform & Runtime team, this role is responsible hands-on for runtime and middleware integration, real-time scheduling and latency accounting, high-rate on-vehicle logging, hardware time-stamping and time synchronization, and the layer that delivers imagery and inertial data into the autonomy stack.
Responsibilities 
- Implement and maintain the autonomy runtime and middleware integration layer, including module interfaces, process and thread structure, message passing and execution rates, that all other autonomy software is developed against
- Own real-time scheduling, latency accounting and deadline monitoring on target hardware, including end-to-end sensor-to-actuator latency measurement under sustained thermal load, and deliver the instrumentation required for latency to be enforced as a release gate
- Design and implement high-rate on-vehicle logging, including ring buffer and triggered capture, multi-sensor and video streams, storage and bandwidth budgeting, and log integrity verification, without degrading control loop timing
- Implement hardware time-stamping and time synchronization across sensors, mission compute and flight controller, and ensure clock domains and synchronization state are represented in logged data such that flight logs support deterministic replay
- Support hardware-in-the-loop bench infrastructure and field test operations, including on-site flight test support, and resolve platform-level timing, logging and integration defects identified in flight data
You should have the following
- 5+ years of professional experience developing production embedded, robotics or real-time software, with a track record of individual technical delivery on systems that operate outside a laboratory
- Demonstrated delivery of software onto a physical vehicle or embedded platform, such as unmanned aircraft, ground or aerial robotics, autonomous vehicles, automotive or aerospace systems, including responsibility through bring-up, integration and field operation rather than simulation or research alone
- Expert proficiency in modern C++ on embedded Linux, and working proficiency in Python for tooling, analysis and automation, developed under real-time and resource-constrained requirements
- Hands-on development within a robotics or vehicle software runtime or middleware layer, covering inter-process communication and message passing, module interface definition, process and thread structure, and execution scheduling, using ROS 2, DDS, Zenoh, LCM, or a proprietary equivalent
- Depth in real-time systems engineering, including scheduling policies and priorities, CPU affinity and isolation, lock-free or zero-copy data paths, jitter and deadline analysis, and measurement of end-to-end latency distributions on target hardware rather than in simulation
- Experience building high-rate on-vehicle data logging, including ring buffer and triggered capture, multi-sensor and video streams, storage and bandwidth budgeting, and log integrity, delivered without degrading control loop timing
- Experience implementing time synchronization and hardware timestamping across sensors, compute and a flight or vehicle controller, using PTP or gPTP, PPS, sensor trigger lines, or equivalent, and reconciling multiple clock domains in logged data
- Experience developing and debugging sensor drivers and data paths for cameras and inertial sensors, including interfaces such as MIPI CSI-2, GMSL, USB3 Vision, GigE Vision, SPI or I2C, and management of calibration data alongside the sensor streams it applies to
Nice to have
- Unmanned aircraft experience specifically, including integration between an onboard mission computer and a flight controller or autopilot using PX4, ArduPilot and MAVLink or a proprietary equivalent, and familiarity with arming logic, flight modes and failsafe behavior at that boundary
- Experience with thermal or other non-visible imaging payloads, including sensor selection support, driver bring-up, and the calibration and data-handling differences relative to visible-spectrum cameras
- Design or build of hardware-in-the-loop benches, including sensor simulation and injection, hardware timestamp instrumentation, and integration of a HIL bench into automated release gating
- Experience supporting deterministic log replay for downstream evaluation, including log schema design, provenance and versioning of recorded data, and reconciling recorded sensor timing with offline ground truth such as RTK GNSS post-processing
US Salary Range
$163,500 – $228,500 USD

## Lead RF Test & Integration Engineer — Torrance, California, United States — id 5226790007 — updated 2026-09-30

https://job-boards.greenhouse.io/nerostechnologies/jobs/5226790007

What you will be doing
As a Lead RF Test & Integration Engineer, you will own the qualification and production test strategy for the wireless systems that make Neros FPV drones work in the real world. You will lead RF validation for video and command-and-control links, build the test infrastructure that catches problems before they reach the field, and oversee NPI and manufacturing acceptance testing as products move from engineering prototypes into production. You will directly manage RF test engineers and technicians, set the technical bar for test execution, and partner with design engineering and manufacturing to close the loop between failures, root cause, corrective action, and production readiness.
Responsibilities
- Own RF qualification testing for FPV drone video links, command-and-control links, antennas, RF front ends, and integrated aircraft/ground systems
- Define validation plans, pass/fail criteria, test procedures, data review methods, and release gates for wireless subsystem qualification
- Lead NPI test readiness for new RF products, including EVT/DVT/PVT planning, fixture readiness, station bring-up, GR&R, and factory handoff
- Oversee manufacturing acceptance testing for RF assemblies and integrated products, including conducted RF tests, radiated tests, range/performance checks, and automated production test limits
- Build, improve, and scale RF test infrastructure using VNAs, spectrum analyzers, signal generators, power meters, SDRs, chambers/OTA setups, Python automation, and production test software
- Drive root-cause investigations for RF desense, interference, range degradation, yield loss, calibration drift, fixture issues, and field-return failures
- Manage and mentor a team of RF test engineers and technicians; set priorities, review technical work, and make sure testing is fast, rigorous, and repeatable
- Partner with RF design, electrical engineering, embedded software, manufacturing, supply chain, and quality to turn test results into design fixes and production controls
You should have the following
- Bachelor of Science in Electrical Engineering, Electrical and Computer Engineering, RF/Microwave Engineering, or a related technical field
- 7+ years of hands-on RF test, RF validation, RF systems, wireless hardware, or production test experience
- Experience qualifying wireless products or RF subsystems through engineering validation, design validation, production validation, or equivalent release processes
- Strong fundamentals in RF measurements: S-parameters, output power, EVM/BER/PER, sensitivity, noise figure, linearity, phase noise, spurious emissions, desense, coexistence, link budget, and antenna performance
- Hands-on experience with RF lab equipment such as spectrum analyzers, VNAs, signal generators, power meters, oscilloscopes, SDRs, chambers/OTA setups, and automated test stations
- Experience building or maintaining automated test systems, ideally using Python or similar scripting tools
- Demonstrated ability to lead engineers and technicians, run test campaigns, prioritize work, review data, and communicate release risk to engineering and manufacturing leaders
- Strong understanding of DFT, production test limits, fixture validation, manufacturing yield, calibration, GR&R, and NPI handoff
Nice to have
- Drone RF systems: Experience testing wireless links on UAVs, FPV drones, and telemetry systems
- Video and C2 links: Experience validating digital video links, analog video links, LoRa command-and-control links, telemetry links, or mesh/point-to-point radio systems
- Manufacturing scale-up: Experience moving RF products from prototype into production, including factory test deployment, fixture design, operator instructions, yield analysis, and corrective action
- OTA and range testing: Experience with chamber testing, antenna pattern/radiated performance, conducted-to-radiated correlation, range tests, and real-world link margin validation
US Salary Range
$162,500 – $227,500 USD

## Senior EW Test & Evaluation Engineer — Torrance, California, United States — id 5253709007 — updated 2026-10-01

https://job-boards.greenhouse.io/nerostechnologies/jobs/5253709007

What you will be doing
As the Senior EW Test & Evaluation Engineer, you will own the full test loop for our system against jamming threats—planning and executing both benchtop and radiated (over-the-air) test campaigns, characterizing system performance against jammers, and identifying the countermeasures needed to defeat them. Working closely with our design team, you will derive and document the specifications required to implement those countermeasures, then verify them through iterative re-testing. This role requires a strong RF background and the ability to translate hands-on test findings into actionable design requirements that harden our system in the field.
 Responsibilities 
- Plan, design, and execute test campaigns against jamming threats across both benchtop and radiated (over-the-air) environments, owning test setup, instrumentation, and calibration
- Characterize system performance under jamming to identify failure modes, vulnerabilities, and the conditions that degrade the link or capability
- Determine effective countermeasures and anti-jam techniques (e.g., frequency agility, spatial nulling, waveform and power management) to defeat identified threats
- Derive and document the specifications and design requirements needed to implement those countermeasures in a form the design team can act on
- Collaborate with counter-UAS engineers to understand threat behavior and ensure test scenarios reflect realistic jamming tactics
- Verify implemented changes through iterative re-testing, confirming the system meets derived specs and closing the test-characterize-spec-retest loop
You should have the following
- Bachelor's or Master's degree in Electrical Engineering, RF/Microwave Engineering, or a related technical field
- 7+ years of experience in RF systems, electronic warfare, or communications test and evaluation, with demonstrated ownership of test campaigns from planning through execution and reporting
- Strong RF fundamentals: link budgets, antenna theory and patterns, RF propagation, modulation and waveforms, and low-probability-of-intercept/detection (LPI/LPD) concepts
- Hands-on experience with benchtop RF test instrumentation, including spectrum/signal analyzers, vector signal generators, vector network analyzers (VNAs), and power meters
- Experience with radiated/over-the-air (OTA) testing, including anechoic or reverb chambers, open-air ranges, calibrated antennas, path-loss accounting, and measurement uncertainty
- Working knowledge of jamming and anti-jam techniques (e.g., barrage, swept, and reactive jamming; frequency hopping, DSSS, spatial nulling, adaptive filtering, power management)
- Demonstrated ability to translate test results into derived specifications and design requirements, and to verify them through iterative re-testing
- Proficiency with data analysis and test automation tools (e.g., MATLAB, Python) for processing and presenting measurement data
Nice to have
- Direct experience in counter-UAS (CUAS) or UAS systems, or working alongside CUAS engineering teams
- Familiarity with DoD developmental test and evaluation (DT&E) processes, test planning documentation, and reporting standards
- Experience designing or integrating anti-jam waveforms, frequency-agile systems, or adaptive/nulling antenna architectures
- Active U.S. security clearance (Secret or higher), or eligibility to obtain one
- Hands-on experience with jammer/threat-emulation hardware or EW signal-generation systems
US Salary Range
$187,000 – $261,500 USD