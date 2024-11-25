#include "ecodan.h"
#include <string>
#include <algorithm>

namespace esphome {
namespace ecodan 
{

    #define MAX_FIRST_LETTER_SIZE 8
    char FaultCodeFirstChar[MAX_FIRST_LETTER_SIZE] = {
        'A', 'b', 'E', 'F', 'J', 'L', 'P', 'U'
    };

    #define MAX_SECOND_LETTER_SIZE 21
    char FaultCodeSecondChar[MAX_SECOND_LETTER_SIZE] = {
        '1', '2', '3', '4', '5', '6', '7', '8', '9',
        'A', 'B', 'C', 'D', 'E', 'F', 'O', 'H', 'J', 'L', 'P', 'U'
    };

    std::string decode_error(uint8_t first, uint8_t second, uint16_t code) {
        std::string fault_code = std::string("No error");
        if (code == 0x6999) {
            fault_code = std::string("Bad communication with unit");
        } else if (code != 0x8000) {
            uint16_t translated_code = (code >> 8) * 100 + (code & 0xff);
            char result[256];
            snprintf(result, 256, "%c%c %u", 
                    FaultCodeFirstChar[std::max<u_int8_t>(0, first & (MAX_FIRST_LETTER_SIZE-1))], 
                    FaultCodeSecondChar[std::max<u_int8_t>(0, second - 1) & (MAX_SECOND_LETTER_SIZE-1)], 
                    translated_code);
            fault_code = std::string(result);
        }

        return fault_code;
    }

    void EcodanHeatpump::handle_get_response(Message& res)
    {
        {
            switch (res.payload_type<GetType>())
            {
            case GetType::DATETIME_FIRMWARE:
                {
                    status.ControllerDateTime.tm_year = 100 + res[1];
                    status.ControllerDateTime.tm_mon = res[2] - 1;
                    status.ControllerDateTime.tm_mday = res[3];
                    status.ControllerDateTime.tm_hour = res[4];
                    status.ControllerDateTime.tm_min = res[5];
                    status.ControllerDateTime.tm_sec = res[6];                    

                    char firmware[6];
                    snprintf(firmware, 6, "%02X.%02X", res[7], res[8]);
                    publish_state("controller_firmware_text", std::string(firmware));                
                }
                break;               
            case GetType::DEFROST_STATE:
                status.DefrostActive = res[3] != 0;
                publish_state("status_defrost", status.DefrostActive);
                break;
            case GetType::ERROR_STATE:
                // 1 = refrigerant error code
                // 2+3 = fault code, [2]*100+[3]
                // 4+5 = fault code: letter 2, 0x00 0x03 = A3
                status.RefrigerantErrorCode = res[1];
                status.FaultCodeNumeric = res.get_u16(2);
                status.FaultCodeLetters = res.get_u16(4);

                publish_state("refrigerant_error_code", static_cast<float>(status.RefrigerantErrorCode));
                publish_state("fault_code_text", decode_error(res[4], res[5], status.FaultCodeNumeric));
                break;
            case GetType::COMPRESSOR_FREQUENCY:
                status.CompressorFrequency = res[1];
                publish_state("compressor_frequency", static_cast<float>(status.CompressorFrequency));
                break;
            case GetType::DHW_STATE:
                // 6 = heat source , 0x0 = heatpump, 0x1 = screw in heater, 0x2 = electric heater..
                status.HeatSource = res[6];
                //status.DhwForcedActive = res[7] != 0 && res[5] == 0; // byte 5 -> 7 is normal dhw, 0 - forced dhw
                //publish_state("status_dhw_forced", status.DhwForcedActive ? "On" : "Off");

                publish_state("heat_source", static_cast<float>(status.HeatSource));
                break;
            case GetType::HEATING_POWER:
                status.OutputPower = res[6];
                //status.BoosterActive = res[4] == 2;
                publish_state("output_power", static_cast<float>(status.OutputPower));
                publish_state("computed_output_power", status.ComputedOutputPower);
                //publish_state("status_booster", status.BoosterActive ? "On" : "Off");
                break;
            case GetType::TEMPERATURE_CONFIG:
                status.Zone1SetTemperature = res.get_float16(1);
                status.Zone2SetTemperature = res.get_float16(3);
                status.Zone1FlowTemperatureSetPoint = res.get_float16(5);
                status.Zone2FlowTemperatureSetPoint = res.get_float16(7);
                status.LegionellaPreventionSetPoint = res.get_float16(9);
                status.DhwTemperatureDrop = res.get_float8_v2(11);
                status.MaximumFlowTemperature = res.get_float8_v2(12);
                status.MinimumFlowTemperature = res.get_float8_v2(13);

                publish_state("z1_room_temp_target", status.Zone1SetTemperature);
                publish_state("z2_room_temp_target", status.Zone2SetTemperature);
                publish_state("z1_flow_temp_target", status.Zone1FlowTemperatureSetPoint);
                publish_state("z2_flow_temp_target", status.Zone2FlowTemperatureSetPoint);

                publish_state("legionella_prevention_temp", status.LegionellaPreventionSetPoint);
                publish_state("dhw_flow_temp_drop", status.DhwTemperatureDrop);
                //ESP_LOGW(TAG, res.debug_dump_packet().c_str());
                // min/max flow

                break;
            case GetType::SH_TEMPERATURE_STATE:
                // 0xF0 seems to be a sentinel value for "not reported in the current system"
                status.Zone1RoomTemperature = res[1] != 0xF0 ? res.get_float16(1) : 0.0f;
                status.Zone2RoomTemperature = res[3] != 0xF0 ? res.get_float16(3) : 0.0f;
                status.OutsideTemperature = res.get_float8(11);
                status.HpRefrigerantLiquidTemperature = res.get_float16_signed(8);
                status.HpRefrigerantCondensingTemperature = res.get_float8(10);

                publish_state("z1_room_temp", status.Zone1RoomTemperature);
                publish_state("z2_room_temp", status.Zone2RoomTemperature);
                publish_state("outside_temp", status.OutsideTemperature);
                publish_state("hp_refrigerant_temp", status.HpRefrigerantLiquidTemperature); 
                publish_state("hp_refrigerant_condensing_temp", status.HpRefrigerantCondensingTemperature);                
                break;
            case GetType::TEMPERATURE_STATE_A:
                status.HpFeedTemperature = res.get_float16(1);
                status.HpReturnTemperature = res.get_float16(4);
                status.DhwTemperature = res.get_float16(7);
                status.DhwSecondaryTemperature = res.get_float16(10); 

                publish_state("hp_feed_temp", status.HpFeedTemperature);
                publish_state("hp_return_temp", status.HpReturnTemperature);
                publish_state("dhw_temp", status.DhwTemperature);
                publish_state("dhw_secondary_temp", status.DhwSecondaryTemperature);
                status.update_output_power_estimation();
                break;
            case GetType::TEMPERATURE_STATE_B:
                status.Z1FeedTemperature = res.get_float16(1);
                status.Z1ReturnTemperature = res.get_float16(4);

                status.Z2FeedTemperature = res.get_float16(7);
                status.Z2ReturnTemperature = res.get_float16(10);

                publish_state("z1_feed_temp", status.Z1FeedTemperature);
                publish_state("z1_return_temp", status.Z1ReturnTemperature);
                publish_state("z2_feed_temp", status.Z2FeedTemperature);
                publish_state("z2_return_temp", status.Z2ReturnTemperature);                                  
                break;
            case GetType::TEMPERATURE_STATE_C:
                status.BoilerFlowTemperature = res.get_float16(1);
                status.BoilerReturnTemperature = res.get_float16(4);
                publish_state("boiler_flow_temp", status.BoilerFlowTemperature);
                publish_state("boiler_return_temp", status.BoilerReturnTemperature);   
                break;
            case GetType::TEMPERATURE_STATE_D:
                status.MixingTankTemperature = res.get_float16(1);
                publish_state("mixing_tank_temp", status.MixingTankTemperature);     
                break;  
            case GetType::EXTERNAL_STATE:
                // 1 = IN1 Thermostat heat/cool request
                // 2 = IN6 Thermostat 2
                // 3 = IN5 outdoor thermostat
                status.In1ThermostatRequest = res[1] != 0;
                status.In6ThermostatRequest = res[2] != 0;
                status.In5ThermostatRequest = res[3] != 0;
                publish_state("status_in1_request", status.In1ThermostatRequest);
                publish_state("status_in6_request", status.In6ThermostatRequest);
                publish_state("status_in5_request", status.In5ThermostatRequest);
                break;
            case GetType::ACTIVE_TIME:
                status.Runtime = res.get_float24_v2(3);
                publish_state("runtime", status.Runtime);
                //ESP_LOGI(TAG, res.debug_dump_packet().c_str());
                break;
            case GetType::PUMP_STATUS:
                // byte 1 = pump running on/off
                // byte 4 = pump 2
                // byte 5 = pump 3
                // byte 6 = 3 way valve on/off
                // byte 7 - 3 way valve 2   
                // byte 10 - Mixing valve step         
                // byte 11 - Mixing valve status
                status.WaterPumpActive = res[1] != 0;
                status.ThreeWayValveActive = res[6] != 0;
                status.WaterPump2Active = res[4] != 0;
                status.ThreeWayValve2Active = res[7] != 0;         
                status.WaterPump3Active = res[5] != 0;       
                status.MixingValveStep = res[10];   
                status.MixingValveStatus = res[11];   
                publish_state("status_water_pump", status.WaterPumpActive);
                publish_state("status_three_way_valve", status.ThreeWayValveActive);
                publish_state("status_water_pump_2", status.WaterPump2Active);
                publish_state("status_three_way_valve_2", status.ThreeWayValve2Active);
                publish_state("status_water_pump_3", status.WaterPump3Active);
                publish_state("status_mixing_valve", static_cast<float>(status.MixingValveStatus));
                publish_state("mixing_valve_step", static_cast<float>(status.MixingValveStep));
                //ESP_LOGI(TAG, res.debug_dump_packet().c_str());
                break;              
            case GetType::FLOW_RATE:
                // booster = 2, 
                // emmersion = 5
                status.BoosterActive = res[2] != 0;
                status.ImmersionActive = res[5] != 0;
                status.FlowRate = res[12];
                publish_state("flow_rate", static_cast<float>(status.FlowRate));
                publish_state("status_booster", status.BoosterActive);
                publish_state("status_immersion", status.ImmersionActive);
                status.update_output_power_estimation();
                break;
            case GetType::MODE_FLAGS_A:
                status.set_power_mode(res[3]);
                status.set_operation_mode(res[4]);
                status.set_dhw_mode(res[5]);
                status.HeatingCoolingMode = static_cast<Status::HpMode>(res[6]);
                status.HeatingCoolingModeZone2 = static_cast<Status::HpMode>(res[7]);
                status.DhwFlowTemperatureSetPoint = res.get_float16(8);
                //status.RadiatorFlowTemperatureSetPoint = res.get_float16(12);

                publish_state("status_power", status.Power == Status::PowerMode::ON);
                publish_state("status_dhw_eco", status.HotWaterMode == Status::DhwMode::ECO);
                publish_state("status_operation", static_cast<float>(status.Operation));
                publish_state("status_heating_cooling", static_cast<float>(status.HeatingCoolingMode));
                publish_state("status_heating_cooling_z2", static_cast<float>(status.HeatingCoolingModeZone2));

                publish_state("dhw_flow_temp_target", status.DhwFlowTemperatureSetPoint);
                //publish_state("sh_flow_temp_target", status.RadiatorFlowTemperatureSetPoint);
                break;
            case GetType::MODE_FLAGS_B:
                status.DhwForcedActive = res[3] != 0;
                status.ProhibitDhw = res[5] != 0;
                status.HolidayMode = res[4] != 0;
                status.ProhibitHeatingZ1 = res[6] != 0;
                status.ProhibitCoolingZ1 = res[7] != 0;
                status.ProhibitHeatingZ2 = res[8] != 0;
                status.ProhibitCoolingZ2 = res[9] != 0;

                publish_state("status_dhw_forced", status.DhwForcedActive);
                publish_state("status_holiday", status.HolidayMode);
                publish_state("status_prohibit_dhw", status.ProhibitDhw);
                publish_state("status_prohibit_heating_z1", status.ProhibitHeatingZ1);
                publish_state("status_prohibit_cool_z1", status.ProhibitCoolingZ1);
                publish_state("status_prohibit_heating_z2", status.ProhibitHeatingZ2);
                publish_state("status_prohibit_cool_z2", status.ProhibitCoolingZ2);
                break;
            case GetType::ENERGY_USAGE:
                status.EnergyConsumedHeating = res.get_float24(4);
                status.EnergyConsumedCooling = res.get_float24(7);
                status.EnergyConsumedDhw = res.get_float24(10);

                publish_state("heating_consumed", status.EnergyConsumedHeating);
                publish_state("cool_consumed", status.EnergyConsumedCooling);
                publish_state("dhw_consumed", status.EnergyConsumedDhw);
                break;
            case GetType::ENERGY_DELIVERY:
                status.EnergyDeliveredHeating = res.get_float24(4);
                status.EnergyDeliveredCooling = res.get_float24(7);
                status.EnergyDeliveredDhw = res.get_float24(10);

                publish_state("heating_delivered", status.EnergyDeliveredHeating);
                publish_state("cool_delivered", status.EnergyDeliveredCooling);
                publish_state("dhw_delivered", status.EnergyDeliveredDhw);
                
                publish_state("heating_cop", status.EnergyConsumedHeating > 0.0f ? status.EnergyDeliveredHeating / status.EnergyConsumedHeating : 0.0f);
                publish_state("cool_cop", status.EnergyConsumedCooling > 0.0f ? status.EnergyDeliveredCooling / status.EnergyConsumedCooling : 0.0f);
                publish_state("dhw_cop", status.EnergyConsumedDhw > 0.0f ? status.EnergyDeliveredDhw / status.EnergyConsumedDhw : 0.0f);

                break;
            case GetType::HARDWARE_CONFIGURATION:
                // byte 6 = ftc, ft2b , ftc4, ftc5, ftc6
                status.Controller = res[6];
                publish_state("controller_version", static_cast<float>(status.Controller));
                break;
            default:
                ESP_LOGI(TAG, "Unknown response type received on serial port: %u", static_cast<uint8_t>(res.payload_type<GetType>()));
                break;
            }
        }
    }

    void EcodanHeatpump::handle_connect_response(Message& res)
    {
        ESP_LOGI(TAG, "connection reply received from heat pump");

        connected = true;
    }

    void EcodanHeatpump::handle_response(Message& res) {
        switch (res.type())
        {
        case MsgType::SET_RES:
            handle_set_response(res);
            break;
        case MsgType::GET_RES:
        case MsgType::CONFIGURATION_RES:
            handle_get_response(res);
            break;
        case MsgType::CONNECT_RES:
            handle_connect_response(res);
            break;
        default:
            ESP_LOGI(TAG, "Unknown serial message type received: %#x", static_cast<uint8_t>(res.type()));
            break;
        }
    }

    void EcodanHeatpump::handle_set_response(Message& res)
    {
        //ESP_LOGW(TAG, res.debug_dump_packet().c_str());
        if (!cmdQueue.empty())
            cmdQueue.pop();

        if (res.type() != MsgType::SET_RES)
        {
            ESP_LOGI(TAG, "Unexpected set response type: %#x", static_cast<uint8_t>(res.type()));
        }          
    }
}
}
