export module ai;

// Building blocks of the game AI shared by every creature (E6):
//   :perception   what an agent knows about the others: sight, hearing, blows, what its pack told it (AI3)
//   :brain        how a brain is put together: modes decided a few times per second, actions run every tick,
//                 a log of the decisions for the debug UI (AI4)
//   :tokens       who may attack whom at once (attack tokens on the targets)
// The behaviour of a species (its brain) lives with the species (gameplay:jackal) and is built from these.
export import :perception;
export import :brain;
export import :tokens;
