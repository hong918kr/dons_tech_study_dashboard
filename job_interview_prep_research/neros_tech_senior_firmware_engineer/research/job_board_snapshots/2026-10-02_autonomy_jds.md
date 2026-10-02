# Neros autonomy 계열 공고 원문 (2026-10-02)


## Autonomy Lead — id 5101791007 — updated 2026-09-30

https://job-boards.greenhouse.io/nerostechnologies/jobs/5101791007

What you will be doing
As the Autonomy Lead, you will own the development of Neros' next autonomy product from the ground up. You'll lead a small, high-performing team of autonomy engineers while directly driving the software architecture and defining hardware requirements for partner teams to design around. Day-to-day, you'll be in the code — developing terminal guidance, position hold, visual navigation, and swarming capabilities across current and future Neros platforms. This is a technical leadership role: you'll set the direction, make the hard calls, and ship capability that gets into soldiers' hands.
Responsibilities
- Lead the autonomy software development effort for an upcoming Neros product, owning architecture decisions end-to-end
- Define hardware requirements and interface closely with hardware teams to ensure software and system design are tightly coupled
- Develop and iterate on terminal guidance, position hold, visual navigation, and swarming capabilities
- Hire, manage, and grow a small team of autonomy engineers — setting technical standards and maintaining a high execution bar
- Write and review production-quality embedded autonomy code in C, C++, and/or Python
- Operate effectively under hardware constraints, making smart tradeoffs between performance and compute/power budgets
You should have the following
- 3+ years proven experience leading technical work in hardware-constrained autonomy — small drones, ground robots, or similar systems strongly preferred
- Hands-on depth in one or more of: computer vision, state estimation, controls, or robotics
- Experience managing engineers and delivering projects end-to-end, not just contributing to them
- Strong proficiency in C, C++, and/or Python in embedded or resource-constrained environments
- Experience hiring for autonomy or AI roles
Nice to have
- Prior work with quadcopters or other aerospace systems
- Familiarity with Betaflight, ExpressLRS, or ArduPilot
US Salary Range
$190,000 - $266,000 USD

## Autonomy Mechatronics Engineer (Hardware & Test) — id 5057431007 — updated 2026-09-30

https://job-boards.greenhouse.io/nerostechnologies/jobs/5057431007

What you will be doing
As an Autonomy Mechatronics Engineer (Hardware & Test), you will own the physical stack that enables autonomy at Neros. You will design, integrate, and test mechanical systems that directly enable autonomous behavior, then prove them under realistic operating conditions. This role spans early-stage concept work through field testing, with direct ownership of both the hardware and the experimental loop. We are looking for engineers who are comfortable moving between CAD, code, in-field testing, and production, and who have demonstrated exceptional ability across the full robotics stack.
Responsibilities 
- Own autonomy-critical mechanical subsystems from concept through testing and deployment
- Design, build, and execute test infrastructure and field experiments to validate real-world performance
- Diagnose and resolve cross-disciplinary failures across mechanics, autonomy, sensing, and manufacturing
You should have the following
- 2+ years of professional experience in mechanical engineering, robotics, or mechatronics roles, with direct ownership of shipped hardware systems
- Strong proficiency in mechanical design and CAD (e.g., SolidWorks, NX, or equivalent), including tolerance analysis and production drawings
- Hands-on experience designing and executing hardware test campaigns, including lab, environmental, and field testing
- Demonstrated ability to debug complex, cross-disciplinary issues involving mechanical systems, sensors, controls, and embedded software
- Experience with rapid prototyping methods (CNC, sheet metal, composites, additive manufacturing) and iterative design cycles
- Working knowledge of structural, thermal, or dynamic analysis using FEA or first-principles calculations
- Familiarity with autonomy or robotics systems (UAVs, UGVs, or similar), including an understanding of how mechanical design impacts sensing and control performance
- Experience supporting manufacturing and scale-up, including build procedures, fixtures, and collaboration with contract manufacturers or suppliers
 Nice to have
- Experience running a test qualification campaign for a robotic or drone product
- Experience characterizing sensors
US Salary Range
$121,000 - $169,500 USD

## Autonomy Platform & Runtime Lead — id 5234134007 — updated 2026-09-30

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

## Manager, Autonomy Evaluation, Data & Test — id 5230189007 — updated 2026-09-30

https://job-boards.greenhouse.io/nerostechnologies/jobs/5230189007

What you will be doing
Neros builds autonomy for drones that operate in contested environments where GPS and communications are denied. The Autonomy Evaluation, Data & Test team owns how autonomy performance is measured across all Neros platforms: the regression and simulation infrastructure every software change is tested against, the data platform that ingests and indexes flight logs, the hardware-in-the-loop bench, and field test operations. As the leader of this team, you will define the metrics and acceptance criteria that determine whether autonomy software is cleared to fly, and build the team and systems that produce that evidence.
- Own the release criteria for autonomy software, including the definition of the regression gate, the conditions under which a build is blocked, and the process for reviewing exceptions
- Define autonomy performance metrics and their semantics for the organization, and publish coverage and reliability reporting to engineering and company leadership
- Build and operate a multi-tier regression system spanning unit and module tests, open-loop replay against logged flights, closed-loop simulation, and hardware-in-the-loop testing on production flight computers, including latency and deadline-miss measurement on target hardware
- Own the autonomy data platform end to end: on-vehicle capture, field egress, ingest and normalization, log catalog and query layer, deterministic replay at scale, and versioned dataset materialization for model training
- Direct field test and autonomy integration, including instrumented test articles, autonomy payload integration, range operations, log recovery, and flight-readiness execution
- Hire, lead, and develop a team of engineers spanning software infrastructure, data engineering, simulation, and physical test
You should have the following
- 8+ years of engineering experience in robotics, autonomous vehicles, aerospace, or defense autonomy, including 3+ years managing engineers
- Direct experience leading an evaluation, validation, metrics, or test infrastructure function for a physical autonomous system
- Demonstrated experience defining performance metrics and acceptance criteria for autonomy or machine learning systems, and owning release gating decisions
- Hands-on experience building automated regression and CI infrastructure for robotics or autonomy software, in Python and C++
- Experience with large-scale log and sensor data infrastructure: ingest pipelines, time alignment across sensors, cataloging and query over multi-terabyte unstructured video and telemetry data, and deterministic replay
- Working knowledge of closed-loop simulation and hardware-in-the-loop testing, including real-time latency budgets and timing measurement on embedded compute
- Experience managing engineers across more than one discipline, including at least one outside your own technical background
- BS or MS in Computer Science, Robotics, Electrical Engineering, Aerospace Engineering, or a related field
Nice to Have
- Experience building evaluation, simulation, or data infrastructure from the ground up at an autonomous vehicle, drone, or robotics company, rather than operating an established system
- Experience with machine learning data pipelines, including dataset versioning and lineage, labeling operations, and large-scale automated model evaluation
- Experience supporting or leading flight test and range operations, including instrumented test articles, test article turnaround, and flight-readiness review processes
- Background in guidance, navigation and control, multi-target tracking, sensor fusion, or electro-optical and infrared perception in maritime or air-to-air domains
US Salary Range
$191,500 – $268,000 USD

## Perception Lead — id 5241175007 — updated 2026-10-01

https://job-boards.greenhouse.io/nerostechnologies/jobs/5241175007

What you will be doing
The Perception Lead owns the perception stack across all Neros unmanned aircraft, covering detection, recognition, and image-space target tracking from the sensor through to the target track consumed by state estimation and guidance. This role leads a small perception engineering team while contributing directly to the software, and sets the technical direction for the transition from classical computer vision to learned models as flight data justifies it. The Perception Lead is accountable for perception performance under operationally degraded conditions, including long standoff, low light and thermal imaging, cluttered land and maritime backgrounds, occlusion, and loss of communications.
- Own the architecture, roadmap, and delivery of the perception stack across all vehicle platforms, maintaining a single configurable implementation rather than per-program forks
- Lead and grow the perception engineering team through hiring, mentorship, and technical review, while continuing to contribute directly as an engineer
- Deliver detection, recognition, and image-space tracking that hold a designated target and re-acquire it after loss, in clutter, glare, low light, occlusion, and degraded link conditions
- Define the perception output contract, including calibrated confidence and explicit signaling of suppressed or degraded detection, and set detection operating points jointly with the state estimation and engagement authorization owners
- Drive the progression from classical computer vision to learned detection and classification, determining when flight data demonstrates that a learned component outperforms the deployed baseline, defining the evaluation criteria and regression cases that decision rests on, and retaining a deterministic fallback
- Own deployment of perception software to constrained onboard compute, including accuracy and latency trade-offs, optimization for embedded accelerators, and compliance with end-to-end latency budgets
You should have the following
- 6+ years developing production computer vision or perception software, including 2+ years leading a team or a technical program as a tech lead or engineering manager, with continued hands-on technical contribution
- Demonstrated delivery of perception software onto a physical system operating in an uncontrolled outdoor environment, such as unmanned aircraft, ground robotics, autonomous vehicles, or aerospace platforms
- Expert proficiency in C++ and Python, including production experience under real-time or resource-constrained requirements
- Depth in both classical and learned perception, spanning correlation, template and optical-flow tracking, feature matching and data association, alongside modern detection and classification networks
- Experience deploying neural networks to embedded accelerators such as NVIDIA Jetson and TensorRT, Qualcomm, Hailo, or equivalent, including quantization and characterization of the resulting accuracy and latency trade-off
- Working knowledge of EO and IR/thermal imaging, camera calibration, and sensor characteristics, with experience in small-object detection at long range, in low light, or under high dynamic range
- Experience defining perception evaluation, including dataset and labeling requirements, detection and tracking metrics such as precision, recall, track continuity and identity switches, and regression testing against logged flight or field data
- Experience selecting detection thresholds and operating points against an explicit cost of false positives versus missed detections in a safety-critical or mission-critical system
- Familiarity with sensor timing and synchronization, coordinate frames, and uncertainty representation at the interface to state estimation and navigation
Nice to Have
- Experience selecting and integrating thermal/LWIR sensors, including the imaging trade-offs and procurement constraints that come with them
- Experience building or operating a data engine for a fleet, covering large-scale log capture, indexing and search, dataset versioning, and labeling operations that feed model training
- Working familiarity with state estimation and sensor fusion, such as visual-inertial odometry or Kalman filtering, sufficient to reason fluently across the boundary between perception and navigation
- Experience with flight test or field test operations, including instrumented test articles, range operations, and log recovery
US Salary Range
$201,000 – $281,000 USD

## Senior Data Platform Engineer — id 5241234007 — updated 2026-10-01

https://job-boards.greenhouse.io/nerostechnologies/jobs/5241234007

What you will be doing
The Data Platform Engineer owns the autonomy data pipeline end to end: recovering flight data from vehicles in the field, landing it in the cloud, and making it searchable and replayable at scale. This is a greenfield role. You will stand up the storage, catalog, and query infrastructure that every autonomy test, simulation run, and machine learning dataset at Neros depends on, and you will own its architecture, operating cost, and compliance posture.
Responsibilities 
- Build the path that brings flight data back from the field, including triggered capture on the vehicle, prioritized upload so the highest-value flights return first, and resumable transfer with integrity verification.
- Turn raw logs into a usable corpus: decode, time-align multi-sensor and video streams, validate, and quarantine malformed data before it reaches downstream users.
- Design the catalog and tag model that index the corpus, and stand up the cloud storage and database that hold it
- Build the query layer so an engineer can retrieve every flight matching a condition, for example loss of target lock at terminal stage under high glare within the last 90 days, and get playable video back in seconds.
- Serve logs to the evaluation harness with stable ordering, exact time alignment, and reproducible results across runs, so a regression job can run over thousands of flights at once.
- Build versioned, immutable datasets from catalog queries, with lineage recorded so any model training set can be rebuilt exactly months later.
You should have the following
- 5+ years building production data or backend infrastructure, including at least one system you owned end to end from initial design through ongoing operation
- Direct experience with large-scale log or sensor data: multi-terabyte and growing, with video and multiple synchronized sensor streams (rosbag, MCAP, HDF5, Parquet, or equivalent formats), rather than row-oriented business data
- Designed and owned a data schema, index, or catalog that other engineers queried daily, and lived with the consequences of that design, including at least one migration
- Strong Python, plus SQL and working ownership of a relational database (PostgreSQL or equivalent) used in production
- Practical experience with cloud object storage and compute (Azure, AWS, or GCP) and the ability to provision and operate it independently, without a dedicated platform or DevOps team
- Experience with distributed or parallel batch processing and job orchestration (Spark, Ray, Dask, Airflow, Dagster, or equivalent) across large volumes of recorded dat
- Working knowledge of time synchronization and alignment across sensor streams, and of deterministic, reproducible processing of recorded data
- A track record of building internal tooling that other engineers adopted, including at least one case where you changed the design based on how it was actually being used
Nice to have
- Log or data infrastructure built for robotics, autonomous vehicles, aerospace, or defense programs, where replay determinism, time alignment, and multi-sensor logs are native problems rather than new ones
- Experience handling video at scale, including transcoding, frame-accurate seeking, and streaming playback of recorded footage to engineering users
- Direct Azure experience, infrastructure defined as code (Terraform or Bicep), and prior responsibility for a cloud budget where storage tiering and egress costs mattered
- Prior work in export-controlled, GovCloud, or IL4/IL5 environments, or familiarity with CMMC and ITAR data handling obligations
- Experience connecting a data pipeline to a labeling vendor or internal labeling tooling, or to model training, experiment tracking, and model registry workflows
US Salary Range
$163,500 – $228,500 USD

## Senior GNC Engineer — id 5241219007 — updated 2026-10-01

https://job-boards.greenhouse.io/nerostechnologies/jobs/5241219007

What you will be doing
The GNC Engineer owns terminal guidance and the associated control path across Neros unmanned aircraft, covering the guidance law, aimpoint command, achievable-acceleration management, and the interface to flight control. This role is accountable for engagement accuracy against maneuvering targets under realistic conditions, including imperfect target state estimates, degraded or lost sensor track, and limited control authority late in the engagement. 
You should have the following
- 5+ years of professional experience developing guidance, navigation, and control software for missiles, interceptors, guided munitions, unmanned aircraft, or comparable autonomous vehicles, with demonstrated ownership of a guidance or control function through flight
- Expert-level knowledge of terminal homing guidance, including proportional and augmented proportional navigation, zero-effort-miss formulations, time-to-go estimation, lead-pursuit geometry, and miss-distance analysis against maneuvering targets
- Demonstrated delivery of guidance or control software onto a physical air vehicle that has flown, not just in simulation
- Production proficiency in C++ and Python, including embedded or real-time development under deterministic execution and latency constraints on resource-limited flight computers
- Working depth in state estimation and sensor fusion, including Kalman and extended Kalman filtering, target motion models, covariance propagation, and observability analysis
- Experience with flight dynamics and control, including autopilot and inner-loop interfaces. Including control authority and actuator saturation limits, achievable-acceleration envelopes, and position or velocity hold control
- Demonstrated use of flight or field test data for closed-loop analysis, including log-based replay, reconstruction of onboard decisions, and root-cause investigation of anomalous engagements
- Fluency with coordinate frame conventions, time synchronization, sensor latency accounting, and uncertainty representation across software interfaces
- Experience participating directly in flight or field test operations, including test planning, instrumentation requirements, range operations, and post-test data review
Nice to have
- Experience with terminal guidance against surface or ground targets, including aimpoint selection on an extended target, impact-angle or impact-point shaping, and engagement geometry planning
- Experience developing safety-critical or high-consequence decision logic, such as engagement authorization, abort criteria, or failsafe behavior, together with the supporting test evidence and written rationale
- Experience with deterministic, reproducible software design for flight-critical components, including bit-exact replay from logged data.
- Background in autonomous vehicles, robotics, or another domain with mature closed-loop evaluation practice, including regression suites, scenario libraries, and release gating on measured performance
US Salary Range
$163,500 – $228,500 USD

## Senior Perception Engineer — id 5241230007 — updated 2026-10-01

https://job-boards.greenhouse.io/nerostechnologies/jobs/5241230007

What you will be doing
The Senior Perception Engineer develops the perception software that allows Neros unmanned aircraft to detect, classify, and track targets from the air, working across the full perception stack rather than a single component. This role covers detection, classification, image-space tracking, and aimpoint selection on both EO and thermal imagery, delivered onto constrained onboard compute and validated against logged flight data. As one of the most senior individual contributors in the perception group, this engineer also provides design and code review across the team's work.
Responsibilities 
- Develop and deliver detection, classification, and image-space tracking for small, low-contrast targets at long standoff in cluttered terrain
- Extend the perception stack to IR and low-light imaging in support of night operations, including sensor frontend integration, performance characterization, and the imagery collection required to support subsequent learned components
- Develop onboard target search and candidate generation that enables operator authorization of engagements over narrowband or degraded communications links
- Implement discrimination of valid targets from civilian, friendly, and neutral objects, and set detection operating points jointly with the state estimation and engagement authorization owners
- Define the perception output contract, including calibrated confidence and explicit signaling of no detection, suppressed detection, and degraded sensor conditions
- Deploy perception software to embedded compute in partnership with the platform team, owning the accuracy and latency trade-off and compliance with end-to-end latency budgets
- Define and maintain perception evaluation and regression cases against logged flight data, and provide senior technical review across the perception team
You should have the following
- 5+ years of professional experience developing production computer vision or perception software, with demonstrated ownership of a perception component from prototype through fielded deployment
- Demonstrated delivery of perception software onto a physical system operating in an uncontrolled outdoor environment, such as unmanned aircraft, ground robotics, autonomous vehicles, or aerospace platforms, rather than research or benchmark work alone
- Expert proficiency in C++ and Python, including production experience under real-time or resource-constrained requirements
- Depth in both classical and learned perception, spanning correlation, template, and optical-flow tracking, feature matching and data association, alongside modern detection and classification networks
- Experience deploying neural networks to embedded accelerators, including quantization and characterization of the resulting accuracy and latency trade-off
- Working knowledge of EO and IR/thermal imaging, camera calibration, and sensor characteristics, with direct experience in small-object detection at long range, in low light, or under high dynamic range
- Experience defining perception evaluation, including dataset and labeling requirements, detection and tracking metrics such as precision, recall, track continuity, and identity switches, and regression testing against logged flight or field data
- Experience selecting detection thresholds and operating points against an explicit cost of false positives versus missed detections in a safety-critical or mission-critical system
- Familiarity with coordinate frames, sensor timing and synchronization, and uncertainty representation at the interface between perception and state estimation
Nice to have
- Experience with air-to-ground EO/IR imagery, or with airborne target acquisition, seeker, or automatic target recognition systems
- Experience selecting or integrating thermal/LWIR sensors, including the imaging and procurement trade-offs involved
- Experience building or contributing to a data engine for a fleet, covering large-scale log capture, indexing and search, dataset versioning, and labeling operations that feed model training
US Salary Range
$163,500 – $228,500 USD

## Senior Platform Engineer — id 5241249007 — updated 2026-10-01

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

## State Estimation & Navigation Lead — id 5230346007 — updated 2026-09-30

https://job-boards.greenhouse.io/nerostechnologies/jobs/5230346007

What you will be doing
The State Estimation and Navigation Lead owns the team responsible for determining vehicle state and target state onboard Neros aircraft, along with the uncertainty attached to each. This team delivers the estimate that guidance, control, and mission behavior are built on, and its accuracy directly bounds the performance of the autonomy stack. You will lead a small team of state estimation and navigation engineers while remaining hands on in the estimator itself, delivering GPS denied navigation across multiple platforms in active development and defining how estimation performance is measured across the autonomy organization.
- Own the technical roadmap for onboard state estimation and navigation, and the interface this team delivers to guidance and control, including vehicle state, target state, and calibrated uncertainty
- Architect, implement, and field GPS denied and GPS degraded navigation to a bounded and measured accuracy specification, fusing IMU, vision, GNSS, and other onboard sensing on embedded compute
- Lead the development of visual inertial and terrain relative navigation from prototype through fielded capability across multiple aircraft
- Define and enforce the standard for characterizing estimation uncertainty and reporting autonomy reliability, including validation in simulation, dataset replay, hardware in the loop, and flight test
- Hire, manage, and mentor state estimation and navigation engineers, and set the technical bar through design and code review on the estimation path
- Partner with perception, guidance and control, platform software, and hardware teams on sensing, compute, latency, and calibration requirements, and support flight test and field deployment of the estimation stack
You should have the following
- 5+ years of professional experience in state estimation, navigation, sensor fusion, or GNC for robotics, aerospace, autonomous vehicles, or defense systems, including experience leading engineers as a manager or technical lead
- Demonstrated ownership of a state estimation or navigation system taken from architecture through deployment on production hardware, not simulation alone
- Deep expertise in estimation theory and its practical application, including EKF, UKF, error state or invariant filters, and factor graph or sliding window smoothing, along with the observability reasoning behind selecting among them
- Hands on experience fusing IMU, camera, GNSS, and range or altimetry sensing, including calibration, time synchronization, latency compensation, bias modeling, and outlier rejection
- Experience delivering navigation performance in GPS denied or GPS degraded environments
- Strong C++ and Python, with experience implementing real time algorithms under embedded compute, memory, and latency constraints
- A track record of quantitative validation, including defining accuracy, drift, and consistency metrics and building the simulation, dataset replay, hardware in the loop, and flight test workflows that measure them
- Experience debugging estimation failures on real hardware in the field and converting them into reproducible regression tests
- Strong foundations in linear algebra, probability, and numerical optimization
Nice to Have
- Experience with visual inertial odometry or terrain relative navigation delivered as a fielded capability rather than a research result
- Experience with target state estimation or tracking through clutter and occlusion, including range, bearing rate, and closing velocity
- Experience building or contributing to small UAS flight stacks such as PX4, ArduPilot, or Betaflight, or to a custom flight control system
- Experience defining evaluation or performance measurement standards that multiple teams report against
- Experience scaling an autonomy stack across multiple vehicle platforms as a configuration rather than a fork
- Prior work in a high tempo startup environment or on a program operating under ITAR or EAR constraints
US Salary Range
$191,500 – $268,000 USD