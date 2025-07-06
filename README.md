# Cisco-UET
UET Attempt on Cisco 8K NPUs using SONiC

https://ultraethernet.org/ultra-ethernet-consortium-experiences-exponential-growth-in-support-of-ethernet-for-high-performance-ai-and-hpc-networking/

┌─────────────────────────────────────────────────────────────┐
│                     SONiC Applications                     │
│  ┌─────────────┐ ┌─────────────┐ ┌─────────────────────────┐│
│  │   orchagent │ │    bgpd     │ │  NEW: ue-linkd          ││
│  │             │ │             │ │       ue-transportd     ││
│  └─────────────┘ └─────────────┘ └─────────────────────────┘│
├─────────────────────────────────────────────────────────────┤
│                      SWSS (Switch State Service)           │
│  ┌─────────────────────────────────────────────────────────┐│
│  │                  SAI (Switch Abstraction Interface)    ││
│  │  + NEW: SAI_OBJECT_TYPE_UE_LLR                         ││
│  │  + NEW: SAI_OBJECT_TYPE_UE_PRI                         ││
│  └─────────────────────────────────────────────────────────┘│
├─────────────────────────────────────────────────────────────┤
│                    Hardware Drivers                        │
│  ┌─────────────────────────────────────────────────────────┐│
│  │  Cisco Silicon One SDK + P4 Programs                   ││
│  └─────────────────────────────────────────────────────────┘│
└─────────────────────────────────────────────────────────────┘

