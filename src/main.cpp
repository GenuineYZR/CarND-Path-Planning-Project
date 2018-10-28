#include <fstream>
#include <math.h>
#include <uWS/uWS.h>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>
#include "Eigen-3.3/Eigen/Core"
#include "Eigen-3.3/Eigen/QR"
#include "json.hpp"
#include "spline.h"

using namespace std;

// for convenience
using json = nlohmann::json;

// For converting back and forth between radians and degrees.
constexpr double pi() { return M_PI; }
double deg2rad(double x) { return x * pi() / 180; }
double rad2deg(double x) { return x * 180 / pi(); }

// Checks if the SocketIO event has JSON data.
// If there is data the JSON object in string format will be returned,
// else the empty string "" will be returned.
string hasData(string s) {
  auto found_null = s.find("null");
  auto b1 = s.find_first_of("[");
  auto b2 = s.find_first_of("}");
  if (found_null != string::npos) {
    return "";
  } else if (b1 != string::npos && b2 != string::npos) {
    return s.substr(b1, b2 - b1 + 2);
  }
  return "";
}

double distance(double x1, double y1, double x2, double y2)
{
	return sqrt((x2-x1)*(x2-x1)+(y2-y1)*(y2-y1));
}
int ClosestWaypoint(double x, double y, const vector<double> &maps_x, const vector<double> &maps_y)
{

	double closestLen = 100000; //large number
	int closestWaypoint = 0;

	for(int i = 0; i < maps_x.size(); i++)
	{
		double map_x = maps_x[i];
		double map_y = maps_y[i];
		double dist = distance(x,y,map_x,map_y);
		if(dist < closestLen)
		{
			closestLen = dist;
			closestWaypoint = i;
		}

	}

	return closestWaypoint;

}

int NextWaypoint(double x, double y, double theta, const vector<double> &maps_x, const vector<double> &maps_y)
{

	int closestWaypoint = ClosestWaypoint(x,y,maps_x,maps_y);

	double map_x = maps_x[closestWaypoint];
	double map_y = maps_y[closestWaypoint];

	double heading = atan2((map_y-y),(map_x-x));

	double angle = fabs(theta-heading);
  angle = min(2*pi() - angle, angle);

  if(angle > pi()/4)
  {
    closestWaypoint++;
  if (closestWaypoint == maps_x.size())
  {
    closestWaypoint = 0;
  }
  }

  return closestWaypoint;
}

// Transform from Cartesian x,y coordinates to Frenet s,d coordinates
vector<double> getFrenet(double x, double y, double theta, const vector<double> &maps_x, const vector<double> &maps_y)
{
	int next_wp = NextWaypoint(x,y, theta, maps_x,maps_y);

	int prev_wp;
	prev_wp = next_wp-1;
	if(next_wp == 0)
	{
		prev_wp  = maps_x.size()-1;
	}

	double n_x = maps_x[next_wp]-maps_x[prev_wp];
	double n_y = maps_y[next_wp]-maps_y[prev_wp];
	double x_x = x - maps_x[prev_wp];
	double x_y = y - maps_y[prev_wp];

	// find the projection of x onto n
	double proj_norm = (x_x*n_x+x_y*n_y)/(n_x*n_x+n_y*n_y);
	double proj_x = proj_norm*n_x;
	double proj_y = proj_norm*n_y;

	double frenet_d = distance(x_x,x_y,proj_x,proj_y);

	//see if d value is positive or negative by comparing it to a center point

	double center_x = 1000-maps_x[prev_wp];
	double center_y = 2000-maps_y[prev_wp];
	double centerToPos = distance(center_x,center_y,x_x,x_y);
	double centerToRef = distance(center_x,center_y,proj_x,proj_y);

	if(centerToPos <= centerToRef)
	{
		frenet_d *= -1;
	}

	// calculate s value
	double frenet_s = 0;
	for(int i = 0; i < prev_wp; i++)
	{
		frenet_s += distance(maps_x[i],maps_y[i],maps_x[i+1],maps_y[i+1]);
	}

	frenet_s += distance(0,0,proj_x,proj_y);

	return {frenet_s,frenet_d};

}

// Transform from Frenet s,d coordinates to Cartesian x,y
vector<double> getXY(double s, double d, const vector<double> &maps_s, const vector<double> &maps_x, const vector<double> &maps_y)
{
	int prev_wp = -1;

	while(s > maps_s[prev_wp+1] && (prev_wp < (int)(maps_s.size()-1) ))
	{
		prev_wp++;
	}

	int wp2 = (prev_wp+1)%maps_x.size();

	double heading = atan2((maps_y[wp2]-maps_y[prev_wp]),(maps_x[wp2]-maps_x[prev_wp]));
	// the x,y,s along the segment
	double seg_s = (s-maps_s[prev_wp]);

	double seg_x = maps_x[prev_wp]+seg_s*cos(heading);
	double seg_y = maps_y[prev_wp]+seg_s*sin(heading);

	double perp_heading = heading-pi()/2;

	double x = seg_x + d*cos(perp_heading);
	double y = seg_y + d*sin(perp_heading);

	return {x,y};

}

int main() {
  uWS::Hub h;

  // Load up map values for waypoint's x,y,s and d normalized normal vectors
  vector<double> map_waypoints_x;
  vector<double> map_waypoints_y;
  vector<double> map_waypoints_s;
  vector<double> map_waypoints_dx;
  vector<double> map_waypoints_dy;

  // Waypoint map to read from
  string map_file_ = "../data/highway_map.csv";
  // The max s value before wrapping around the track back to 0
  double max_s = 6945.554;

  ifstream in_map_(map_file_.c_str(), ifstream::in);

  string line;
  while (getline(in_map_, line)) {
  	istringstream iss(line);
  	double x;
  	double y;
  	float s;
  	float d_x;
  	float d_y;
  	iss >> x;
  	iss >> y;
  	iss >> s;
  	iss >> d_x;
  	iss >> d_y;
  	map_waypoints_x.push_back(x);
  	map_waypoints_y.push_back(y);
  	map_waypoints_s.push_back(s);
  	map_waypoints_dx.push_back(d_x);
  	map_waypoints_dy.push_back(d_y);
  }

  double ref_v = 0;// Set the reference velocity and add the reference to the lambda function below.

  int lane = 1; // Set the lane in order to change the lane line when necessary.

  h.onMessage([&ref_v, &lane, &map_waypoints_x,&map_waypoints_y,&map_waypoints_s,&map_waypoints_dx,&map_waypoints_dy](uWS::WebSocket<uWS::SERVER> ws, char *data, size_t length,
                     uWS::OpCode opCode) {
    // "42" at the start of the message means there's a websocket message event.
    // The 4 signifies a websocket message
    // The 2 signifies a websocket event
    //auto sdata = string(data).substr(0, length);
    //cout << sdata << endl;
	 
    if (length && length > 2 && data[0] == '4' && data[1] == '2') {

      auto s = hasData(data);

      if (s != "") {
        auto j = json::parse(s);
        
        string event = j[0].get<string>();
        
        if (event == "telemetry") {
          // j[1] is the data JSON object
          
        	// Main car's localization Data
          	double car_x = j[1]["x"];
          	double car_y = j[1]["y"];
          	double car_s = j[1]["s"];
          	double car_d = j[1]["d"];
          	double car_yaw = j[1]["yaw"];
          	double car_speed = j[1]["speed"];

          	// Previous path data given to the Planner
          	auto previous_path_x = j[1]["previous_path_x"];
          	auto previous_path_y = j[1]["previous_path_y"];
          	// Previous path's end s and d values 
          	double end_path_s = j[1]["end_path_s"];
          	double end_path_d = j[1]["end_path_d"];

          	// Sensor Fusion Data, a list of all other cars on the same side of the road.
          	auto sensor_fusion = j[1]["sensor_fusion"];

          	json msgJson;

          	vector<double> next_x_vals;
          	vector<double> next_y_vals;


          	// TODO: define a path made up of (x,y) points that the car will visit sequentially every .02 seconds
			
			// Walk through all the other cars around us and try to control the action of our car based on them.
			int pre_size = previous_path_x.size();
			bool too_close = false;
			vector<double> risk_lane0; // Check the s value those cars on the adjacent lane for future use i.e. lane change.
			vector<double> risk_lane1;
			vector<double> risk_lane2;

			if (pre_size > 0)
			{
				car_s = end_path_s;
			}

			for (int i = 0; i < sensor_fusion.size(); i++)
			{				
				double vx = sensor_fusion[i][3];
				double vy = sensor_fusion[i][4];
				double check_d = sensor_fusion[i][6];
				double check_s = sensor_fusion[i][5];
				double check_speed = sqrt(vx * vx + vy * vy);
				double diff_d = 2 + 4 * lane - check_d;
				
				if (abs(diff_d) < 2)
				{										
					check_s += (double)pre_size * 0.02 * check_speed; // If using previous path points can project s out.
					
					if ((check_s > car_s) && ((check_s - car_s) < 20))
					{
						too_close = true;// Flag to change the velocity and change lane.				
					}
				}
				else if (abs(diff_d) > 2 && abs(diff_d) < 6)
				{
					check_s += (double)pre_size * 0.02 * check_speed;
					double diff_s = check_s - car_s;
					
					if ((diff_s > -20) && (diff_s) < 10) // Check if there is any risk to change lane, if the difference of s value is within -20m and 10m, there is a risk.
					{
						if (check_d < 4)
						{
							risk_lane0.push_back(i);
						}
						else if (check_d < 8)
						{
							risk_lane1.push_back(i);
						}
						else
						{
							risk_lane2.push_back(i);
						}
					}
				}
			}

			// If too close, first slow down, then check if there is any chance to change lane.
			if (too_close)
			{
				ref_v -= 0.324;
				
				switch (lane)
				{
				case 0:
					if (risk_lane1.size() == 0)
					{
						lane = 1;
						cout << "change to lane 1" << endl;
					}
					break;
				case 1:
					if (risk_lane0.size() == 0)
					{
						lane = 0;
						cout << "change to lane 0" << endl;
					}
					else if (risk_lane2.size() == 0)
					{
						lane = 2;
						cout << "change to lane 2" << endl;
					}
					break;
				case 2:
					if (risk_lane1.size() == 0)
					{
						lane = 1;
						cout << "change to lane 1" << endl;
					}
					break;
				}
			}
			else if (ref_v < 49.5)
			{
				ref_v += 0.224;
			}
			
			vector<double> spline_x_vals;
			vector<double> spline_y_vals;

			double ref_x = car_x;
			double ref_y = car_y;
			double ref_yaw = deg2rad(car_yaw);
			// If the previous size is almost empty, use the car as the next starting point.
			if (pre_size < 2)
			{
				double ref_x_pre = car_x - cos(ref_yaw);
				double ref_y_pre = car_y - sin(ref_yaw);

				spline_x_vals.push_back(ref_x_pre);
				spline_y_vals.push_back(ref_y_pre);

				spline_x_vals.push_back(ref_x);
				spline_y_vals.push_back(ref_y);
			}
			else
			{
				ref_x = previous_path_x[pre_size - 1];
				ref_y = previous_path_y[pre_size - 1];

				double ref_x_pre = previous_path_x[pre_size - 2];
				double ref_y_pre = previous_path_y[pre_size - 2];

				ref_yaw = atan2(ref_y - ref_y_pre, ref_x - ref_x_pre);

				spline_x_vals.push_back(ref_x_pre);
				spline_y_vals.push_back(ref_y_pre);

				spline_x_vals.push_back(ref_x);
				spline_y_vals.push_back(ref_y);
			}
			
			// Generate 3 more reference points to fit the spline.
			for (int i = 0; i < 3; i++)
			{
				double s_spline = car_s + 30 * (i + 1);
				double d_spline = 2 + 4 * lane;
				vector<double> xy_spline = getXY(s_spline, d_spline, map_waypoints_s, map_waypoints_x, map_waypoints_y);

				spline_x_vals.push_back(xy_spline[0]);
				spline_y_vals.push_back(xy_spline[1]);
			}

			// Shift the map space to car space.
			for (int i = 0; i < spline_x_vals.size(); i++)
			{
				double shift_x = spline_x_vals[i] - ref_x;
				double shift_y = spline_y_vals[i] - ref_y;
				spline_x_vals[i] = shift_x * cos(ref_yaw) + shift_y * sin(ref_yaw);
				spline_y_vals[i] = shift_y * cos(ref_yaw) - shift_x * sin(ref_yaw);
			}
			//cout << spline_x_vals[0] << " " << spline_x_vals[1] << " " << spline_x_vals[2] << " " << spline_x_vals[3] << " " << spline_x_vals[4] << endl;
			// Generate the spline and fit it with the reference points.
			tk::spline s;
			s.set_points(spline_x_vals, spline_y_vals);

			double target_x = 30; // Suppose the car go 30m in the x direction in the car space.
			double target_y = s(target_x);
			double target_dis = distance(0, 0, target_x, target_y);
			
			// First add all the previous points to the path planner.
			for (int i = 0; i < pre_size; i++)
			{
				next_x_vals.push_back(previous_path_x[i]);
				next_y_vals.push_back(previous_path_y[i]);
			}
			// Then add the rest of the points to the path planner.
			double x_start = 0;
			
			for (int i = 0; i < 50 - pre_size; i++)
			{
				double N = target_dis / (0.02 * (ref_v / 2.24));// Calculate the number of points needed for the path planner in order to get the car run within the speed limit.
				double x_point = x_start + target_x / N;
				double y_point = s(x_point);
				x_start = x_point;

				// Shift back to the map space
				double shift_x = x_point;
				double shift_y = y_point;
				x_point = shift_x * cos(-ref_yaw) + shift_y * sin(-ref_yaw);// First rotate.
				y_point = shift_y * cos(-ref_yaw) - shift_x * sin(-ref_yaw);
				
				x_point += ref_x;// Then translate.
				y_point += ref_y;
				
				next_x_vals.push_back(x_point);
				next_y_vals.push_back(y_point);
			}
			
          	msgJson["next_x"] = next_x_vals;
          	msgJson["next_y"] = next_y_vals;

          	auto msg = "42[\"control\","+ msgJson.dump()+"]";

          	//this_thread::sleep_for(chrono::milliseconds(1000));
          	ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
          
        }
      } else {
        // Manual driving
        std::string msg = "42[\"manual\",{}]";
        ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
      }
    }
  });

  // We don't need this since we're not using HTTP but if it's removed the
  // program
  // doesn't compile :-(
  h.onHttpRequest([](uWS::HttpResponse *res, uWS::HttpRequest req, char *data,
                     size_t, size_t) {
    const std::string s = "<h1>Hello world!</h1>";
    if (req.getUrl().valueLength == 1) {
      res->end(s.data(), s.length());
    } else {
      // i guess this should be done more gracefully?
      res->end(nullptr, 0);
    }
  });

  h.onConnection([&h](uWS::WebSocket<uWS::SERVER> ws, uWS::HttpRequest req) {
    std::cout << "Connected!!!" << std::endl;
  });

  h.onDisconnection([&h](uWS::WebSocket<uWS::SERVER> ws, int code,
                         char *message, size_t length) {
    ws.close();
    std::cout << "Disconnected" << std::endl;
  });

  int port = 4567;
  if (h.listen(port)) {
    std::cout << "Listening to port " << port << std::endl;
  } else {
    std::cerr << "Failed to listen to port" << std::endl;
    return -1;
  }
  h.run();
}
