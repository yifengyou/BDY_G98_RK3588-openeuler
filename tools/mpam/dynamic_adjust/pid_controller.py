# SPDX-License-Identifier: GPL-2.0
#
# PID Algorithm Controller for Dynamic Bandwidth Adjustment
#
# Copyright (C) 2025, Technologies Co., Ltd.
# Author: Zeng Heng <zengheng4@huawei.com>

class PID_Controller:
    def __init__(self, kp, ki, kd, set_point):
        self.kp = kp                # Proportional Gain
        self.ki = ki                # Integral Gain
        self.kd = kd                # Derivative Gain
        self.set_point = set_point
        self.last_error = 0
        self.integral = 0           # Integral Term

        self.max_output = 10        # rise slowly
        self.min_output = -100      # fall quickly

    def update(self, current_value, dt):
        """
        Update the PID controller's output
        :param current_value: Current system output value
        :param dt: Time interval
        :return: Controller output
        """
        error = self.set_point - current_value          # Calculate current error
        self.integral += error * dt                     # Update Integral Term
        if self.integral < -100:
            self.integral = -100
        elif self.integral > 100:
            self.integral = 100
        derivative = (error - self.last_error) / dt     # Calculate Derivative Term

        output = (self.kp * error) + (self.ki * self.integral) + (self.kd * derivative)

        self.last_error = error

        # Limit the output range
        if output > self.max_output:
            output = self.max_output
        elif output < self.min_output:
            output = self.min_output

        return output
