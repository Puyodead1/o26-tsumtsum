/* CORE compiles plugins for ES2017 without Node typings, so BigInt is unknown to TypeScript; Node has it at runtime. */
declare const BigInt: (v: number | string) => bigint;
export const big = (n: number | string): bigint => BigInt(n);
