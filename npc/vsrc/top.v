module top(
    input wire a,      // 开关1输入
    input wire b,      // 开关2输入
    output wire f      // 输出控制灯的状态
);
    assign f = a ^ b;  // 通过异或实现双控逻辑
endmodule

