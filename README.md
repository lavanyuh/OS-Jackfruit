# OS Jackfruit Container Runtime

---

## 1. Team Information

- **Jiya Datta Banik** – PES1UG24CS202  
- **Sai Lavanya Kanchinadham** – PES1UG24CS214  

---

## 2. Build, Load and Run Instructions

###  Build the project
```bash
make clean
make
```
   
### Setup root filesystem (alpine)
   ```bash
   mkdir rootfs-base
   tar -xzf alpine-minirootfs-3.19.9-x86_64.tar.gz -C rootfs-base
   ```

### Start supervisor
   ```bash 
    sudo ./engine supervisor ./rootfs-base
   ```

### Check running containers
```bash
ps aux | grep sh
```

### View container metadata
```bash
   ./engine ps
```
   
### Run workload (outside container)
```bash
   ./memory_hog
   ./cpu_hog
```
   
 ### Stop processes
 ```bash
   pkill -9 memory_hog
   pkill -9 cpu_hog
   ```
 ### Kernel logs
 ```bash
   dmesg | tail
```
   
 ### Cleanup
 ```bash
   sudo rmmod monitor
```

## 3. Demo with screenshots
1.

  ![s1](https://github.com/user-attachments/assets/7e3934c9-b8a3-464f-8e14-fc5e05c7cf30)

two containers (alpha and beta) running under one supervisor process


2.
 ![s2](https://github.com/user-attachments/assets/ad487e4b-59db-4532-8763-e0e1c194a577)

output of engine ps showing container IDs and PIDs


3.
 ![s3](https://github.com/user-attachments/assets/d0da0a38-b25c-4c18-8642-357404d857f9)

containers generating logs concurrently demonstrating producer behaviour in logging pipeline


4.
![s4](https://github.com/user-attachments/assets/bceb0132-671f-4144-80a9-18fbe4bf80e8)

CLI issues a request and receives a response from supervisor


5.
![s5](https://github.com/user-attachments/assets/cc316d33-bf8b-427f-b605-d1b0dd93bbcc)

kernel logs show warning when container exceeds soft memory limit 


6.
![s6](https://github.com/user-attachments/assets/19506b52-26ef-483f-8ba9-2b0e6ffc5f8a)

kernel logs show container termination after exceeding hard limit


7.
![s7](https://github.com/user-attachments/assets/5f7fbef8-6ddd-4d4d-96c6-9c5d95efc21b)

CPU-intensive workloads (cpu_hog) shows observable CPU usage differences 


8.
  ![s8](https://github.com/user-attachments/assets/bbf8a587-7424-48a8-b29a-988d51a68f61)

processes are terminated and no zombie processes remain (ps aux verification)


## Engineering Analysis

### Namespace Isolation

The project uses Linux namespaces (PID, UTS, mount) to isolate containers.
Each container runs with its own process tree and filesystem view, ensuring separation from the host system.

### Supervisor Architecture

A single supervisor process is responsible for:

spawning containers

tracking metadata

coordinating execution

This design centralizes control and simplifies management.

### Kernel Monitoring

A kernel module monitors memory usage of container processes.
It periodically checks RSS values and enforces:

soft limits (warnings)

hard limits (process termination)


### IPC Mechanism

Communication between CLI and supervisor is done using a control interface.
This allows commands like ps to query container state.

### Scheduling Behavior

Multiple CPU-intensive workloads demonstrate how Linux distributes CPU time fairly among processes using its scheduler.

## Design Tradeoffs 
### Namespace Design
Choice: Use Linux namespaces

Tradeoff: More complexity in setup

Justification: Provides strong isolation

### Supervisor Model
Choice: Single supervisor process

Tradeoff: Single point of failure

Justification: Easier coordination and debugging

### Logging System
Choice: Bounded buffer logging

Tradeoff: Possible log loss under heavy load

Justification: Prevents unbounded memory growth

### Kernel Module Monitoring
Choice: Kernel-level monitoring

Tradeoff: Risk of system instability if incorrect

Justification: Direct access to process memory metrics

### Scheduling Experiment

Choice: CPU hog processes

Tradeoff: Artificial workload

Justification: Clearly demonstrates scheduler behavior

## Scheduler Experiment Results 

### Observed output
![s8](https://github.com/user-attachments/assets/2b68fbc1-3221-4f4d-bff9-1abeb600d2a6)

### Analysis
- Multiple cpu_hog processes were run simultaneously.

- Each process consumed a high percentage of CPU (close to 100%).

- The CPU usage was distributed between processes rather than one process monopolizing the CPU.

### Key Observations
- The Linux scheduler shares CPU time across competing processes.
  
- Even though both processes are CPU-intensive, they are scheduled fairly.
  
- Slight differences in CPU usage (e.g., 99.9% vs 94.7%) occur due to scheduling decisions and system overhead.
Conclusion

### This experiment demonstrates that the Linux scheduler:

- Allocates CPU time fairly among runnable processes
  
- Prevents starvation
  
- Efficiently handles CPU-bound workloads





   



