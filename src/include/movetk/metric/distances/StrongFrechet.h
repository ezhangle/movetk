/*
 * Copyright (C) 2018-2022 TU Eindhoven
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 * License-Filename: LICENSE
 */

//
// Created by Custers, Bram on 2022-03-11
//

#ifndef MOVETK_METRIC_DISTANCES_STRONGFRECHET_H
#define MOVETK_METRIC_DISTANCES_STRONGFRECHET_H

#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/dijkstra_shortest_paths.hpp>
#include <boost/iterator/transform_iterator.hpp>
#include <iostream>

#include "movetk/geom/GeometryInterface.h"
#include "movetk/utils/Asserts.h"

namespace movetk::metric {

namespace strong_frechet_strategy {
class full_freespace {};
}  // namespace strong_frechet_strategy

template <typename Kernel, typename SqDistance>
class FreespaceDiagram {
public:
	using NT = typename Kernel::NT;
	/**
	 * \brief Polynomial for the freespace cell boundary.
	 */
	struct Polynomial {
		// Parallel-perpendicular decomposition of distance between point and segment
		NT parallelDistance;
		NT perpendicularDistance;
		// Smallest epsilon needed to make the ball centered at the point touch the segment
		NT minimumEpsilon;
		// Type of relation between point and segment:
		// 'i' : parallel projection of the point lies on the segment
		// 'a' : parallel projection of the point lies above the segment (relative to the segment direction)
		// 'b' : parallel projection of the point lies below the segment (relative to the segment direction)
		char type;

		// Non-normalized reachable range
		std::pair<NT, NT> range(NT epsilon) const {
			auto sq = [](auto el) { return el * el; };
			if (epsilon < minimumEpsilon)
				return std::make_pair<NT, NT>(std::numeric_limits<NT>::max(), std::numeric_limits<NT>::lowest());
			return std::make_pair(
			    type == 'b' ? 0.0 : parallelDistance - std::sqrt(sq(epsilon) - sq(perpendicularDistance)),
			    type == 'a' ? parallelDistance : parallelDistance + std::sqrt(sq(epsilon) - sq(perpendicularDistance)));
		}

		void compute(const Point &point, const Point &seg0, const Point &seg1) {
			typename SqDistance::Norm norm;
			auto dir = seg1 - seg0;
			auto segLen = std::sqrt(norm(dir));
			auto pntLen = std::sqrt(norm(point - seg0));
			// Projected value on line through segment,
			// with origin at start of segment
			parallelDistance = (point - seg0) * dir / segLen;
			perpendicularDistance = std::sqrt(pntLen * pntLen - parallelDistance * parallelDistance);
			auto seg = movetk::geom::MakeSegment<Kernel>()(seg0, seg1);
			minimumEpsilon = std::sqrt(SqDistance()(point, seg));
			type = 'i';
			if (parallelDistance > segLen)
				type = 'a';
			else if (parallelDistance < 0)
				type = 'b';
		}
	};

	struct CellPolynomials {
		// Cell boundary polynomials for left (0) and bottom (1) boundaries.
		Polynomial polys[2];
	};
	std::vector<std::vector<CellPolynomials>> parameterized_freespace;
	/**
	 * \brief Precomputes the polynomials describing the freespace cell boundaries.
	 * \tparam PointIt Type of the input point iterators for the polylines
	 * \param polyA First polyline, specified as pair of start and end iterator of points
	 * \param polyB Second polyline, specified as pair of start and end iterator of points
	 * \param polynomials Output table of polynomials.
	 */
	template <typename PointItA, typename PointItB>
	void compute_freespace(const std::pair<PointItA, PointItA> &polyA, const std::pair<PointItB, PointItB> &polyB) {
		// We don't save the boundaries at the top/right of the freespacediagram, since they are not need:
		// by convexity, if a path uses the boundary, then the left/bottom boundaries should have atleast one
		// point of free space.
		const auto polyASize = std::distance(polyA.first, polyA.second);
		const auto polyBSize = std::distance(polyB.first, polyB.second);
		parameterized_freespace.resize(polyASize - 1, {});
		for (auto &el : polynomials) {
			el.resize(polyBSize - 1, CellPolynomials{});
		}
		std::size_t i = 0;
		for (auto pointA = polyA.first; pointA != std::prev(polyA.second); ++pointA, ++i) {
			std::size_t j = 0;
			for (auto pointB = polyB.first; pointB != std::prev(polyB.second); ++pointB, ++j) {
				// Compute bottom boundary polynomial
				parameterized_freespace[i][j].polys[0].compute(*pointA, *pointB, *(std::next(pointB)));
				// Compute left boundary polynomial
				parameterized_freespace[i][j].polys[1].compute(*pointB, *pointA, *std::next(pointA));
			}
		}
	}

	template <typename PointItA, typename PointItB>
	FreespaceDiagram(const std::pair<PointItA, PointItA> &polyA, const std::pair<PointItB, PointItB> &polyB) {
		compute_freespace(polyA, polyB);
	}

	const std::vector<CellPolynomials> &operator[](size_t i) const { return parameterized_freespace[i]; }
	size_t size() const { return parameterized_freespace.size(); }
};

template <typename Kernel, typename SqDistance, typename Approach>
class StrongFrechetPredicate;

template <typename Kernel, typename SqDistance>
class StrongFrechetPredicate<Kernel, SqDistance, strong_frechet_strategy::full_freespace> {
public:
	using NT = typename Kernel::NT;
	using MovetkPoint = typename Kernel::MovetkPoint;

	template <class InputIteratorA,
	          class InputIteratorB,
	          typename = movetk::utils::requires_random_access_iterator<InputIteratorA>,
	          typename = movetk::utils::requires_movetk_point_iterator<Kernel, InputIteratorA>,
	          typename = movetk::utils::requires_random_access_iterator<InputIteratorB>,
	          typename = movetk::utils::requires_movetk_point_iterator<Kernel, InputIteratorB>>
	StrongFrechetPredicate(InputIteratorA poly_a,
	                       InputIteratorA poly_a_beyond,
	                       InputIteratorB poly_b,
	                       InputIteratorB poly_b_beyond) {
	precompute(poly_a, poly_a_beyond, poly_b,poly_b_beyond);
	}


	/**
	 * \brief Given two polylines, decide if the strong Frechet distance is at most epsilon.
	 * \param polynomials Freespace diagram, given as a table of polynomials
	 * \param epsilon The maximum allowed Frechet distance
	 * \return Whether or not the polylines are within Frechet distance epsilon
	 */
	bool operator()(NT epsilon) const {
		// Polyline sizes (number of points)
		if(m_precomputed_distance_sq.has_value()){
			return *m_precomputed_distance_sq <= epsilon * epsilon + m_precision;
		}
		else{
			if(m_parameterized_freespace.size() == 0){return false;}
			return decide(epsilon);
		}

		// If the endpoint epsilon is the smallest, within some fraction, we are ok with selecting that
		return decide(polynomials, epsilon);
	}
	void set_precision(NT precision) { m_precision = precision; }
	NT get_precision() const { return m_precision; }

private:
	template <class InputIteratorA,
	          class InputIteratorB,
	          typename = movetk::utils::requires_random_access_iterator<InputIteratorA>,
	          typename = movetk::utils::requires_movetk_point_iterator<Kernel, InputIteratorA>,
	          typename = movetk::utils::requires_random_access_iterator<InputIteratorB>,
	          typename = movetk::utils::requires_movetk_point_iterator<Kernel, InputIteratorB>>
	void precompute(InputIteratorA poly_a,
	                       InputIteratorA poly_a_beyond,
	                       InputIteratorB poly_b,
	                       InputIteratorB poly_b_beyond) {
		// Polyline sizes (number of points)
		const auto polyASize = std::distance(poly_a, poly_a_beyond);
		const auto polyBSize = std::distance(poly_b, poly_b_beyond);

		// Or error
		if (polyASize == 0 || polyBSize == 0){
				return;
		}
		if (polyASize == 1 || polyBSize == 1) {
			auto single_point_poly = polyASize == 1 ? poly_a : poly_b;
			auto range_poly_start = polyASize == 1 ? poly_b : poly_a;
			auto range_poly_end = polyASize == 1 ? poly_b_beyond : poly_a_beyond;
			std::vector<NT> distances;
			std::transform(range_poly_start,
			               range_poly_end,
			               std::back_insert_iterator(distances),
			               [single_point_poly](const auto &range_poly_element) {
				               SqDistance sqDist;
				               return sqDist(*single_point_poly, range_poly_element);
			               });
			// Prestore distance
			m_precomputed_distance_sq =*std::max_element(std::begin(distances), std::end(distances));
		}
		else if (polyASize == 2 && polyBSize == 2) {
			// Minimum required epsilon to make start and end match for polylines.
			m_precomputed_distance_sq = std::max(m_sqDistance(*poly_a, *poly_b), m_sqDistance(*std::prev(poly_a_beyond), *std::prev(poly_b_beyond)));
		}else{
			m_parameterized_freespace.compute_freespace(std::make_pair(poly_a, poly_a_beyond), std::make_pair(poly_b, poly_b_beyond));
		}
	}

	bool decide(NT epsilon) const {
		const auto epsilon_sq = epsilon*epsilon;
		const auto &polynomials = m_parameterized_freespace;
		const auto maxI = polynomials.size();
		const auto maxJ = polynomials[0].size();

		// Dimensions of the diagram.
		const std::size_t sizes[2] = {maxI, maxJ};

		struct Interval {
			NT min = std::numeric_limits<NT>::max();
			NT max = std::numeric_limits<NT>::lowest();
			Interval() {}
			Interval(NT min, NT max) : min(min), max(max) {}
			bool isEmpty() const { return max < min; }
			void assignMaxToMin(const Interval &other) {
				if (isEmpty())
					return;
				min = std::max(min, other.min);
			}
		};
		struct CellIntervals {
			Interval intervals[2] = {{}, {}};
			bool isReachable() const {
				// Reachable if one of the intervals is not empty
				return !intervals[0].isEmpty() || !intervals[1].isEmpty();
			}
			Interval &operator[](int i) { return intervals[i]; }
			const Interval &operator[](int i) const { return intervals[i]; }
		};
		// Save two rows/columns while computing the decision problem
		std::vector<CellIntervals> progress[2];
		// Which of the two saved rows/columns to fill. Either 0 or 1
		int current = 0;

		// Dimension: 0 = row direction, 1 is column direction
		// Cell intervals in dimesion 0 are the bottom intervals, otherwise the left intervals.
		// Pick the smallest dimension for storing the intermediate state.
		// We then iterate over the other dimension
		const int dim = maxI > maxJ ? 1 : 0;
		// The other dimension, where we will iterate over
		const int secondaryDim = 1 - dim;

		// Get the freespace interval for a cell boundary at the given dimension.
		auto getFreeSpace = [dim, &polynomials, epsilon](int primaryDimIndex, int secondaryDimIndex, int targetDim) {
			const auto r = dim == 0 ? primaryDimIndex : secondaryDimIndex;
			const auto c = dim == 0 ? secondaryDimIndex : primaryDimIndex;
			auto res = polynomials[r][c].polys[targetDim].range(epsilon);
			return Interval{res.first, res.second};
		};

		// Initialize first row(col), depending on chosen dimension to compute over.
		progress[current].assign(sizes[dim], {});
		progress[current][0].intervals[secondaryDim] =
		    Interval(std::numeric_limits<NT>::lowest(), std::numeric_limits<NT>::max());  // Fully open interval
		progress[current][0].intervals[dim] =
		    Interval(std::numeric_limits<NT>::lowest(), std::numeric_limits<NT>::max());  // Fully open interval
		for (auto i = 1; i < sizes[dim]; ++i) {
			if (!progress[current][i - 1].intervals[dim].isEmpty()) {
				progress[current][i].intervals[dim] = getFreeSpace(i, 0, dim);
				progress[current][i].intervals[dim].assignMaxToMin(progress[current][i - 1].intervals[dim]);
			}
		}
		// Go over all other rows(columns).
		for (auto j = 1; j < sizes[secondaryDim]; ++j) {
			// Fill other
			const auto prev = current;
			current = 1 - current;

			// Reset intervals to empty
			progress[current].assign(sizes[dim], {});

			// Compute first cell
			auto &firstCell = progress[current][0];
			const auto &prevFirstCell = progress[prev][0];
			if (!prevFirstCell[secondaryDim].isEmpty()) {
				firstCell[secondaryDim] = getFreeSpace(0, j, secondaryDim);
				// Assign maximum of the minimum coordinates to the minimum
				firstCell[secondaryDim].assignMaxToMin(prevFirstCell[secondaryDim]);
			}

			// Compute the intervals for the other cells
			const auto &prevCells = progress[prev];
			const auto &currCells = progress[current];
			bool hasReachable = firstCell.isReachable();
			for (auto i = 1; i < sizes[dim]; ++i) {
				auto &currCell = progress[current][i];
				// Compute secondary dimension element
				if (prevCells[i].isReachable()) {
					currCell[secondaryDim] = getFreeSpace(i, j, secondaryDim);
					if (prevCells[i][dim].isEmpty() && !prevCells[i][secondaryDim].isEmpty()) {
						currCell[secondaryDim].assignMaxToMin(prevCells[i][secondaryDim]);
					}
				}
				if (currCells[i - 1].isReachable()) {
					currCell[dim] = getFreeSpace(i, j, dim);
					// Compute primary dimension element
					if (currCells[i - 1][secondaryDim].isEmpty() && !currCells[i - 1][dim].isEmpty()) {
						currCell[dim].assignMaxToMin(currCells[i - 1][dim]);
					}
				}

				hasReachable = currCell.isReachable() || hasReachable;
			}
			// Early out
			if (!hasReachable) {
				return false;
			}
		}

		return progress[current].back().isReachable();
	}

	FreespaceDiagram<Kernel, SqDistance> m_parameterized_freespace;

	// Precomputed distance for degenerate cases.
	std::optional<NT> m_precomputed_distance_sq;
	NT m_precision = 0;
};

/**
 * \brief Strong Frechet distance between polylines
 * Implementation of Alt & Godau in combination with either a double-and-search approach or a
 * bisection (binary search) approach. For the latter, a decent upperbound is needed to determine
 * the search range.
 * See: http://www.staff.science.uu.nl/~kreve101/asci/ag-cfdbt-95.pdf
 * \tparam Kernel The geometry kernel
 * \tparam SqDistance Square distance type for compting segment-point distance.
 */
template <typename Kernel, typename SqDistance>
class StrongFrechet {
	// Typedefs
	using NT = typename Kernel::NT;
	using Point = typename Kernel::MovetkPoint;

	// The norm to use
	SqDistance m_sqDistance;

	// Upperbound on the allowed Frechet distance
	NT m_upperBound = std::numeric_limits<NT>::max();

	// Precision on the output strong Frechet distance
	NT m_precision = 1e-5;

	/**
	 * \brief Polynomial for the freespace cell boundary.
	 */
	struct Polynomial {
		// Parallel-perpendicular decomposition of distance between point and segment
		NT parallelDistance;
		NT perpendicularDistance;
		// Smallest epsilon needed to make the ball centered at the point touch the segment
		NT minimumEpsilon;
		// Type of relation between point and segment:
		// 'i' : parallel projection of the point lies on the segment
		// 'a' : parallel projection of the point lies above the segment (relative to the segment direction)
		// 'b' : parallel projection of the point lies below the segment (relative to the segment direction)
		char type;

		// Non-normalized reachable range
		std::pair<NT, NT> range(NT epsilon) const {
			auto sq = [](auto el) { return el * el; };
			if (epsilon < minimumEpsilon)
				return std::make_pair<NT, NT>(std::numeric_limits<NT>::max(), std::numeric_limits<NT>::lowest());
			return std::make_pair(
			    type == 'b' ? 0.0 : parallelDistance - std::sqrt(sq(epsilon) - sq(perpendicularDistance)),
			    type == 'a' ? parallelDistance : parallelDistance + std::sqrt(sq(epsilon) - sq(perpendicularDistance)));
		}

		void compute(const Point &point, const Point &seg0, const Point &seg1) {
			typename SqDistance::Norm norm;
			auto dir = seg1 - seg0;
			auto segLen = std::sqrt(norm(dir));
			auto pntLen = std::sqrt(norm(point - seg0));
			// Projected value on line through segment,
			// with origin at start of segment
			parallelDistance = (point - seg0) * dir / segLen;
			perpendicularDistance = std::sqrt(pntLen * pntLen - parallelDistance * parallelDistance);
			auto seg = movetk::geom::MakeSegment<Kernel>()(seg0, seg1);
			minimumEpsilon = std::sqrt(SqDistance()(point, seg));
			type = 'i';
			if (parallelDistance > segLen)
				type = 'a';
			else if (parallelDistance < 0)
				type = 'b';
		}
	};

	struct CellPolynomials {
		// Cell boundary polynomials for left (0) and bottom (1) boundaries.
		Polynomial polys[2];
	};

	/**
	 * \brief Precomputes the polynomials describing the freespace cell boundaries.
	 * \tparam PointIt Type of the input point iterators for the polylines
	 * \param polyA First polyline, specified as pair of start and end iterator of points
	 * \param polyB Second polyline, specified as pair of start and end iterator of points
	 * \param polynomials Output table of polynomials.
	 */
	template <typename PointItA, typename PointItB>
	void precomputePolynomials(const std::pair<PointItA, PointItA> &polyA,
	                           const std::pair<PointItB, PointItB> &polyB,
	                           std::vector<std::vector<CellPolynomials>> &polynomials) const {
		// We don't save the boundaries at the top/right of the freespacediagram, since they are not need:
		// by convexity, if a path uses the boundary, then the left/bottom boundaries should have atleast one
		// point of free space.
		const auto polyASize = std::distance(polyA.first, polyA.second);
		const auto polyBSize = std::distance(polyB.first, polyB.second);
		polynomials.resize(polyASize - 1, {});
		for (auto &el : polynomials) {
			el.resize(polyBSize - 1, CellPolynomials{});
		}
		std::size_t i = 0;
		for (auto pointA = polyA.first; pointA != std::prev(polyA.second); ++pointA, ++i) {
			std::size_t j = 0;
			for (auto pointB = polyB.first; pointB != std::prev(polyB.second); ++pointB, ++j) {
				// Compute bottom boundary polynomial
				polynomials[i][j].polys[0].compute(*pointA, *pointB, *(std::next(pointB)));
				// Compute left boundary polynomial
				polynomials[i][j].polys[1].compute(*pointB, *pointA, *std::next(pointA));
			}
		}
	}

	/**
	 * \brief Given the freespace diagram and an epsilon, decide if the strong Frechet distance
	 * is at most epsilon.
	 * It is assumed that the given epsilon is larger than or equal to the smallest distance
	 * of the endpoints of the polylines.
	 * \param polynomials Freespace diagram, given as a table of polynomials
	 * \param epsilon The maximum allowed Frechet distance
	 * \return Whether or not the polylines are within Frechet distance epsilon
	 */
	bool decide(const std::vector<std::vector<CellPolynomials>> &polynomials, NT epsilon) const {
		const auto maxI = polynomials.size();
		const auto maxJ = polynomials[0].size();

		const std::size_t sizes[2] = {maxI, maxJ};

		struct Interval {
			NT min = std::numeric_limits<NT>::max();
			NT max = std::numeric_limits<NT>::lowest();
			Interval() {}
			Interval(NT min, NT max) : min(min), max(max) {}
			bool isEmpty() const { return max < min; }
			void assignMaxToMin(const Interval &other) {
				if (isEmpty())
					return;
				min = std::max(min, other.min);
			}
		};
		struct CellIntervals {
			Interval intervals[2] = {{}, {}};
			bool isReachable() const {
				// Reachable if one of the intervals is not empty
				return !intervals[0].isEmpty() || !intervals[1].isEmpty();
			}
			Interval &operator[](int i) { return intervals[i]; }
			const Interval &operator[](int i) const { return intervals[i]; }
		};
		// Save two rows/columns while computing the decision problem
		std::vector<CellIntervals> progress[2];
		// Which of the two saved rows/columns to fill. Either 0 or 1
		int current = 0;

		// Dimension: 0 = row direction, 1 is column direction
		// Cell intervals in dimesion 0 are the bottom intervals, otherwise the left intervals.
		// Pick the smallest dimension for storing the intermediate state.
		// We then iterate over the other dimension
		const int dim = maxI > maxJ ? 1 : 0;
		// The other dimension, where we will iterate over
		const int secondaryDim = 1 - dim;

		// Get the freespace interval for a cell boundary at the given dimension.
		auto getFreeSpace = [dim, &polynomials, epsilon](int primaryDimIndex, int secondaryDimIndex, int targetDim) {
			const auto r = dim == 0 ? primaryDimIndex : secondaryDimIndex;
			const auto c = dim == 0 ? secondaryDimIndex : primaryDimIndex;
			auto res = polynomials[r][c].polys[targetDim].range(epsilon);
			return Interval{res.first, res.second};
		};

		// Initialize first row(col), depending on chosen dimension to compute over.
		progress[current].assign(sizes[dim], {});
		progress[current][0].intervals[secondaryDim] =
		    Interval(std::numeric_limits<NT>::lowest(), std::numeric_limits<NT>::max());  // Fully open interval
		progress[current][0].intervals[dim] =
		    Interval(std::numeric_limits<NT>::lowest(), std::numeric_limits<NT>::max());  // Fully open interval
		for (auto i = 1; i < sizes[dim]; ++i) {
			if (!progress[current][i - 1].intervals[dim].isEmpty()) {
				progress[current][i].intervals[dim] = getFreeSpace(i, 0, dim);
				progress[current][i].intervals[dim].assignMaxToMin(progress[current][i - 1].intervals[dim]);
			}
		}
		// Go over all other rows(columns).
		for (auto j = 1; j < sizes[secondaryDim]; ++j) {
			// Fill other
			const auto prev = current;
			current = 1 - current;

			// Reset intervals to empty
			progress[current].assign(sizes[dim], {});

			// Compute first cell
			auto &firstCell = progress[current][0];
			const auto &prevFirstCell = progress[prev][0];
			if (!prevFirstCell[secondaryDim].isEmpty()) {
				firstCell[secondaryDim] = getFreeSpace(0, j, secondaryDim);
				// Assign maximum of the minimum coordinates to the minimum
				firstCell[secondaryDim].assignMaxToMin(prevFirstCell[secondaryDim]);
			}

			// Compute the intervals for the other cells
			const auto &prevCells = progress[prev];
			const auto &currCells = progress[current];
			bool hasReachable = firstCell.isReachable();
			for (auto i = 1; i < sizes[dim]; ++i) {
				auto &currCell = progress[current][i];
				// Compute secondary dimension element
				if (prevCells[i].isReachable()) {
					currCell[secondaryDim] = getFreeSpace(i, j, secondaryDim);
					if (prevCells[i][dim].isEmpty() && !prevCells[i][secondaryDim].isEmpty()) {
						currCell[secondaryDim].assignMaxToMin(prevCells[i][secondaryDim]);
					}
				}
				if (currCells[i - 1].isReachable()) {
					currCell[dim] = getFreeSpace(i, j, dim);
					// Compute primary dimension element
					if (currCells[i - 1][secondaryDim].isEmpty() && !currCells[i - 1][dim].isEmpty()) {
						currCell[dim].assignMaxToMin(currCells[i - 1][dim]);
					}
				}

				hasReachable = currCell.isReachable() || hasReachable;
			}
			// Early out
			if (!hasReachable) {
				return false;
			}
		}

		return progress[current].back().isReachable();
	}

	/**
	 * \brief Search the epsilon such that the strong Frechet distance is epsilon, within the predefined tolerance. The
	 * given lower and upper bound give the range between which it is known that the value of epsilon should lie. \param
	 * polynomials The freespace diagram, described as a table of polynomials describing the cell boundary freespace
	 * \param tolerance The tolerance to use for determining the epsilon
	 * \param lower The lower bound on the range to search epsilon in
	 * \param upper The upper bound on the range to search epsilon in
	 * \param outDist The output epsilon value
	 * \return Whether or not the epsilon was found in the given range.
	 */
	bool bisectionSearchInInterval(const std::vector<std::vector<CellPolynomials>> &polynomials,
	                               NT tolerance,
	                               NT lower,
	                               NT upper,
	                               NT &outDist) const {
		// Upper should be valid, otherwise we are searching in an infeasible interval.
		if (!decide(polynomials, upper))
			return false;

		NT lBound = lower, rBound = upper;
		NT currentBest = upper;
		while (true) {
			if (std::abs(lBound - rBound) < tolerance)
				break;
			// New value to test
			const NT curr = (lBound + rBound) * 0.5;
			if (decide(polynomials, curr)) {
				rBound = curr;
				currentBest = curr;
			} else {
				lBound = curr;
			}
		}
		outDist = currentBest;

		return true;
	}

	template <typename PointItA, typename PointItB>
	bool bisectionSearchUpperBounded(const std::pair<PointItA, PointItA> &polyA,
	                                 const std::pair<PointItB, PointItB> &polyB,
	                                 NT &outDist) const {
		// Polyline sizes (number of points)
		const auto polyASize = std::distance(polyA.first, polyA.second);
		const auto polyBSize = std::distance(polyB.first, polyB.second);

		// Minimum required epsilon to make start and end match for polylines.
		const NT minEps = std::sqrt(std::max(m_sqDistance(*polyA.first, *polyB.first),
		                                     m_sqDistance(*(polyA.first + polyASize - 1), *(polyB.first + polyBSize - 1))));

		if (minEps > m_upperBound) {
			return false;
		}

		std::vector<std::vector<CellPolynomials>> polynomials;
		precomputePolynomials(polyA, polyB, polynomials);

		// If the endpoint epsilon is the smallest, within some fraction, we are ok with selecting that
		if (decide(polynomials, minEps + m_precision)) {
			outDist = minEps;
			return true;
		}
		return bisectionSearchInInterval(polynomials, m_precision, minEps, m_upperBound, outDist);
	}

	template <typename PointItA, typename PointItB>
	bool doubleAndSearch(const std::pair<PointItA, PointItA> &polyA,
	                     const std::pair<PointItB, PointItB> &polyB,
	                     NT &outDist) {
		// Polyline sizes (number of points)
		const auto polyASize = std::distance(polyA.first, polyA.second);
		const auto polyBSize = std::distance(polyB.first, polyB.second);
		// Minimum required epsilon to make start and end match for polylines.
		NT minEps = std::sqrt(std::max(m_sqDistance(*polyA.first, *polyB.first),
		                               m_sqDistance(*std::prev(polyA.second), *std::prev(polyB.second))));

		if (minEps > m_upperBound) {
			return false;
		}

		std::vector<std::vector<CellPolynomials>> polynomials;
		precomputePolynomials(polyA, polyB, polynomials);

		// If the endpoint epsilon is the smallest, within some fraction, we are ok with selecting that
		if (decide(polynomials, minEps + m_precision)) {
			outDist = minEps + m_precision;
			return true;
		}
		if (minEps < m_precision) {
			minEps = m_precision;
		}

		// Double the epsilon at each step
		NT currEps = minEps * 2.0;
		while (true) {
			// Should happen at some point unless the input is extremely malformed
			if (decide(polynomials, currEps)) {
				return bisectionSearchInInterval(polynomials, m_precision, currEps * 0.5, currEps, outDist);
			}
			currEps *= 2.0;
		}
	}

public:
	enum class Mode { BisectionSearch, DoubleAndSearch };

private:
	Mode m_mode;

public:
	StrongFrechet(Mode mode = Mode::DoubleAndSearch) : m_mode(mode) {}

	/**
	 * \brief Returns the computation mode for the strong Frechet computation
	 * \return The computation mode
	 */
	Mode mode() const { return m_mode; }

	/**
	 * \brief Set the computation mode for the strong Frechet computation
	 * \param mode The computation mode
	 */
	void setMode(Mode mode) { m_mode = mode; }

	/**
	 * \brief Set upperbound for upperbounded search approaches
	 * \param upperBound The upperbound on the Frechet distance
	 */
	void setUpperbound(NT upperBound) { m_upperBound = upperBound; }

	/**
	 * \brief Given two polylines, decide if the strong Frechet distance is at most epsilon.
	 * \param polynomials Freespace diagram, given as a table of polynomials
	 * \param epsilon The maximum allowed Frechet distance
	 * \return Whether or not the polylines are within Frechet distance epsilon
	 */
	template <class InputIteratorA,
	          class InputIteratorB,
	          typename = movetk::utils::requires_random_access_iterator<InputIteratorA>,
	          typename = movetk::utils::requires_movetk_point_iterator<Kernel, InputIteratorA>,
	          typename = movetk::utils::requires_random_access_iterator<InputIteratorB>,
	          typename = movetk::utils::requires_movetk_point_iterator<Kernel, InputIteratorB>>
	bool decide(InputIteratorA poly_a,
	            InputIteratorA poly_a_beyond,
	            InputIteratorB poly_b,
	            InputIteratorB poly_b_beyond,
	            NT epsilon) const {
		// Polyline sizes (number of points)
		const auto polyASize = std::distance(poly_a, poly_a_beyond);
		const auto polyBSize = std::distance(poly_b, poly_b_beyond);

		// Or error
		if (polyASize == 0 || polyBSize == 0)
			return false;

		if (polyASize == 1) {
			// std::function<const NT &(const typename Kernel::MovetkPoint &)> transform = [poly_a](const auto &polyBEl) ->
			// const NT & {
			//     SqDistance sqDist;
			//     return std::sqrt(sqDist(*poly_a, polyBEl));
			// };
			// auto first = boost::make_transform_iterator(poly_b, transform);
			// auto beyond = boost::make_transform_iterator(poly_b_beyond, transform);
			std::vector<NT> distances;
			std::transform(poly_b, poly_b_beyond, std::back_insert_iterator(distances), [poly_a](const auto &polyBEl) {
				SqDistance sqDist;
				return std::sqrt(sqDist(*poly_a, polyBEl));
			});
			auto maxElIt = std::max_element(std::begin(distances), std::end(distances));
			return *maxElIt <= epsilon + m_precision;
		}
		if (polyBSize == 1) {
			// std::function<const NT &(const typename Kernel::MovetkPoint &)> transform = [poly_b](const auto &polyAEl) ->
			// const NT & {
			//     SqDistance sqDist;
			//     return std::sqrt(sqDist(*poly_b, polyAEl));
			// };
			// auto first = boost::make_transform_iterator(poly_a, transform);
			// auto beyond = boost::make_transform_iterator(poly_a_beyond, transform);
			// auto maxElIt = std::max_element(first, beyond);

			std::vector<NT> distances;
			std::transform(poly_a, poly_a_beyond, std::back_insert_iterator(distances), [poly_b](const auto &polyAEl) {
				SqDistance sqDist;
				return std::sqrt(sqDist(*poly_b, polyAEl));
			});
			auto maxElIt = std::max_element(std::begin(distances), std::end(distances));

			return *maxElIt <= epsilon + m_precision;
		}

		// Minimum required epsilon to make start and end match for polylines.
		NT minEps = std::max(std::sqrt(m_sqDistance(*poly_a, *poly_b)),
		                     std::sqrt(m_sqDistance(*std::prev(poly_a_beyond), *std::prev(poly_b_beyond))));

		if (polyASize == 2 && polyBSize == 2) {
			return minEps <= epsilon + m_precision;
		}

		if (minEps > m_upperBound + m_precision || minEps > epsilon + m_precision) {
			return false;
		}

		std::vector<std::vector<CellPolynomials>> polynomials;
		precomputePolynomials(std::make_pair(poly_a, poly_a_beyond), std::make_pair(poly_b, poly_b_beyond), polynomials);

		// If the endpoint epsilon is the smallest, within some fraction, we are ok with selecting that
		return decide(polynomials, epsilon);
	}

	/**
	 * \brief Set the error tolerance in Frechet distance for the inexact methods.
	 * \param tolerance The tolerance
	 */
	void setTolerance(NT tolerance) { m_precision = tolerance; }
	NT tolerance() const { return m_precision; }

	template <class InputIteratorA,
	          class InputIteratorB,
	          typename = movetk::utils::requires_random_access_iterator<InputIteratorA>,
	          typename = movetk::utils::requires_movetk_point_iterator<Kernel, InputIteratorA>,
	          typename = movetk::utils::requires_random_access_iterator<InputIteratorB>,
	          typename = movetk::utils::requires_movetk_point_iterator<Kernel, InputIteratorB>>
	bool operator()(InputIteratorA poly_a,
	                InputIteratorA poly_a_beyond,
	                InputIteratorB poly_b,
	                InputIteratorB poly_b_beyond,
	                typename Kernel::NT &output) {
		auto polyA = std::make_pair(poly_a, poly_a_beyond);
		auto polyB = std::make_pair(poly_b, poly_b_beyond);

		// Polyline sizes (number of points)
		const auto polyASize = std::distance(poly_a, poly_a_beyond);
		const auto polyBSize = std::distance(poly_b, poly_b_beyond);

		// Or error
		if (polyASize == 0 || polyBSize == 0)
			return false;

		if (polyASize == 1) {
			auto sqDist = m_sqDistance;
			// Use std::function, otherwise boost will error when trying to copy the transform_iterator
			// std::function<NT(const typename Kernel::MovetkPoint &)> transform = [poly_a](const auto &polyBEl) {
			//     SqDistance sqDist;
			//     return std::sqrt(sqDist(*poly_a, polyBEl));
			// };

			// auto maxElIt = std::max_element(boost::make_transform_iterator(poly_b, transform),
			// boost::make_transform_iterator(poly_b_beyond, transform));

			std::vector<NT> distances;
			std::transform(poly_b, poly_b_beyond, std::back_insert_iterator(distances), [poly_a](const auto &polyBEl) {
				SqDistance sqDist;
				return std::sqrt(sqDist(*poly_a, polyBEl));
			});
			auto maxElIt = std::max_element(std::begin(distances), std::end(distances));

			output = *maxElIt;
			return output <= m_upperBound + m_precision;
		}
		if (polyBSize == 1) {
			// // Use std::function, otherwise boost will error when trying to copy the transform_iterator
			// std::function<NT(const typename Kernel::MovetkPoint &)> transform = [poly_b](const auto &polyAEl) {
			//     SqDistance sqDist;
			//     return std::sqrt(sqDist(*poly_b, polyAEl));
			// };
			// auto maxElIt = std::max_element(boost::make_transform_iterator(poly_a, transform),
			// boost::make_transform_iterator(poly_a_beyond, transform));

			std::vector<NT> distances;
			std::transform(poly_a, poly_a_beyond, std::back_insert_iterator(distances), [poly_b](const auto &polyAEl) {
				SqDistance sqDist;
				return std::sqrt(sqDist(*poly_b, polyAEl));
			});
			auto maxElIt = std::max_element(std::begin(distances), std::end(distances));

			output = *maxElIt;
			return output <= m_upperBound + m_precision;
		}

		switch (m_mode) {
			case Mode::BisectionSearch: return bisectionSearchUpperBounded(polyA, polyB, output);
			case Mode::DoubleAndSearch: return doubleAndSearch(polyA, polyB, output);
			default: return false;
		}
	}

	template <class InputIteratorA,
	          class InputIteratorB,
	          typename = movetk::utils::requires_random_access_iterator<InputIteratorA>,
	          typename = movetk::utils::requires_movetk_point_iterator<Kernel, InputIteratorA>,
	          typename = movetk::utils::requires_random_access_iterator<InputIteratorB>,
	          typename = movetk::utils::requires_movetk_point_iterator<Kernel, InputIteratorB>>
	typename Kernel::NT operator()(InputIteratorA poly_a,
	                               InputIteratorA poly_a_beyond,
	                               InputIteratorB poly_b,
	                               InputIteratorB poly_b_beyond) {
		typename Kernel::NT epsilon = -1;
		(void)this->operator()(poly_a, poly_a_beyond, poly_b, poly_b_beyond, epsilon);
		return epsilon;
	}
};
}  // namespace movetk::metric
#endif