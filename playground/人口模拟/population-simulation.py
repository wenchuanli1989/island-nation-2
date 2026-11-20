#!/usr/bin/env python3
"""离散月度人口模拟：衰老、死亡、婚配、生育与统计；可选存活人口上限。"""

import argparse
import csv
import math
import os
import random
import select
import sys
import time
import termios
import tty
import unicodedata

_stdin_termios_backup = None
_stdin_cbreak_enabled = False

# ============================================================
#  全局模拟参数（可直接修改）
# ============================================================

DOMINO_MIN_BEAR_AGE = 3               # 最小生育年龄（年）
DOMINO_MAX_BEAR_AGE = 18               # 最大生育年龄（年）
DOMINO_MAX_HUMAN_AGE = 24              # 最大寿命（年）
MAX_CHILDREN_PER_COUPLE = 5     # 每人一生最大生育子女数（跨婚姻累计）
MIN_BIRTH_INTERVAL = 12         # 两次生育最小间隔（月）
MAX_CHILDREN_PER_BIRTH = 3      # 每次生育最大子女数（>1 可模拟多胞胎）
PREGNANCY_PROB_PER_YEAR = 0.9   # 育龄黄金期每年怀孕概率（实际按年龄抛物线衰减）
MALE_BIRTH_PROB = 0.51          # 生男概率
MARRIAGE_PROB_PER_YEAR = 0.9    # 每年结婚成功概率（首婚）
REMARRIAGE_PROB_PER_YEAR = 0.3  # 配偶死亡后每年再婚概率（叠加到结婚概率上）

# 死亡概率参数
BASE_DEATH_PROB_PER_YEAR = 0.05   # 超过 DOMINO_MAX_BEAR_AGE 时的基础年死亡概率（随年龄递增）
INFANT_DEATH_PROB_PER_YEAR = 0.03 # 0~1 岁婴幼儿年死亡概率
ACCIDENT_DEATH_PROB_PER_YEAR = 0.005  # 所有年龄段意外死亡年概率

# 婚龄上限：男女可不同
MALE_MARRIAGE_MAX_AGE = 20    # 男性婚配年龄上限（年）
FEMALE_MARRIAGE_MAX_AGE = 18  # 女性婚配年龄上限（年）

# 亲属禁婚检查深度（向上追溯几代祖先）
KIN_CHECK_DEPTH = 3

# 存活人口上限
POPULATION_CAP = 10000

# 两次月度迭代之间的等待秒数
INTER_STEP_SLEEP_SEC = 1.0

# 子女世代基准：'father' | 'mother' | 'max'
CHILD_GENERATION_FROM = 'father'

# C 端 generation 字段为 uint8_t，最大 255
MAX_GENERATION = 255

# ============================================================
#  性别常量
# ============================================================

MALE = 1
FEMALE = 0

# ============================================================
#  概率转换工具
# ============================================================


def _yearly_to_monthly_prob(p_year: float) -> float:
    """将年概率精确转换为等效月概率：p_month = 1 - (1 - p_year)^(1/12)"""
    if p_year <= 0.0:
        return 0.0
    if p_year >= 1.0:
        return 1.0
    return 1.0 - math.pow(1.0 - p_year, 1.0 / 12.0)


MIN_BEAR_AGE_MONTHS = DOMINO_MIN_BEAR_AGE * 12
MAX_BEAR_AGE_MONTHS = DOMINO_MAX_BEAR_AGE * 12
MAX_HUMAN_AGE_MONTHS = DOMINO_MAX_HUMAN_AGE * 12
MALE_MARRIAGE_MAX_AGE_MONTHS = MALE_MARRIAGE_MAX_AGE * 12
FEMALE_MARRIAGE_MAX_AGE_MONTHS = FEMALE_MARRIAGE_MAX_AGE * 12

MONTHLY_MARRIAGE_PROBABILITY = _yearly_to_monthly_prob(MARRIAGE_PROB_PER_YEAR)
MONTHLY_REMARRIAGE_PROBABILITY = _yearly_to_monthly_prob(
    min(1.0, MARRIAGE_PROB_PER_YEAR + REMARRIAGE_PROB_PER_YEAR)
)
MONTHLY_ACCIDENT_DEATH_PROBABILITY = _yearly_to_monthly_prob(ACCIDENT_DEATH_PROB_PER_YEAR)
MONTHLY_INFANT_DEATH_PROBABILITY = _yearly_to_monthly_prob(INFANT_DEATH_PROB_PER_YEAR)
MALE_BIRTH_RANDOM_THRESHOLD = MALE_BIRTH_PROB


# ============================================================
#  初始人口数据
#  格式: (ID, 性别, 年龄, 配偶ID(-1=未婚), 子女ID列表, 父亲ID(-1=无), 母亲ID(-1=无))
# ============================================================

INIT_POPULATION = [
    (0,  MALE,   5,  1, [], -1, -1),
    (1,  FEMALE, 5,  0, [], -1, -1),
    (2,  MALE,   5,  3, [], -1, -1),
    (3,  FEMALE, 5,  2, [], -1, -1),
    (4,  MALE,   5,  5, [], -1, -1),
    (5,  FEMALE, 5,  4, [], -1, -1),
    (6,  MALE,   5, -1, [], -1, -1),
    (7,  FEMALE, 5, -1, [], -1, -1),
    (8,  MALE,   5, -1, [], -1, -1),
    (9,  FEMALE, 5, -1, [], -1, -1),
    (10, MALE,   5, -1, [], -1, -1),
    (11, FEMALE, 5, -1, [], -1, -1),
    (12, MALE,   5, -1, [], -1, -1),
    (13, FEMALE, 5, -1, [], -1, -1),
    (14, MALE,   5, -1, [], -1, -1),
    (15, FEMALE, 5, -1, [], -1, -1),
    (16, MALE,   5, -1, [], -1, -1),
    (17, FEMALE, 5, -1, [], -1, -1),
    (18, MALE,   5, -1, [], -1, -1),
    (19, FEMALE, 5, -1, [], -1, -1),
    (20, MALE,   5, -1, [], -1, -1),
    (21, FEMALE, 5, -1, [], -1, -1),
    (22, MALE,   5, -1, [], -1, -1),
    (23, FEMALE, 5, -1, [], -1, -1),
    (24, MALE,   5, -1, [], -1, -1),
    (25, FEMALE, 5, -1, [], -1, -1),
    (26, MALE,   5, -1, [], -1, -1),
    (27, FEMALE, 5, -1, [], -1, -1),
    (28, MALE,   5, -1, [], -1, -1),
    (29, FEMALE, 5, -1, [], -1, -1),
    (30, MALE,   5, -1, [], -1, -1),
    (31, FEMALE, 5, -1, [], -1, -1),
    (32, MALE,   5, -1, [], -1, -1),
    (33, FEMALE, 5, -1, [], -1, -1),
    (34, MALE,   5, -1, [], -1, -1),
    (35, FEMALE, 5, -1, [], -1, -1),
    (36, MALE,   5, -1, [], -1, -1),
    (37, FEMALE, 5, -1, [], -1, -1),
    (38, MALE,   5, -1, [], -1, -1),
    (39, FEMALE, 5, -1, [], -1, -1),
    (40, MALE,   5, -1, [], -1, -1),
    (41, FEMALE, 5, -1, [], -1, -1),
    (42, MALE,   5, -1, [], -1, -1),
    (43, FEMALE, 5, -1, [], -1, -1),
    (44, MALE,   5, -1, [], -1, -1),
    (45, FEMALE, 5, -1, [], -1, -1),
    (46, MALE,   5, -1, [], -1, -1),
    (47, FEMALE, 5, -1, [], -1, -1),
    (48, MALE,   5, -1, [], -1, -1),
    (49, FEMALE, 5, -1, [], -1, -1),
]

# ============================================================
#  Person 类
# ============================================================


class Person:
    """单个人员：年龄以月存储，便于与 MIN_BIRTH_INTERVAL 等按月参数对齐。"""
    __slots__ = (
        'id', 'gender', 'age_months', 'spouse_id',
        'father_id', 'mother_id', 'children',
        'last_birth_sim_month', 'alive', 'widowed', 'generation',
        'first_marriage_month',
    )

    def __init__(self, pid, gender, age_years, spouse_id=-1,
                 children=None, father_id=-1, mother_id=-1,
                 last_birth_month=-999, generation=1):
        self.id = pid
        self.gender = gender
        self.age_months = age_years * 12
        self.spouse_id = spouse_id
        self.father_id = father_id
        self.mother_id = mother_id
        self.children = list(children) if children else []
        self.last_birth_sim_month = last_birth_month
        self.alive = True
        self.widowed = False
        self.generation = generation
        self.first_marriage_month = -1  # 首次结婚时的模拟月序号（-1=从未结婚）

    @property
    def age_years(self):
        return self.age_months // 12


# ============================================================
#  模拟器
# ============================================================


class Simulation:
    """维护全体 Person，按「月」推进：先长一岁，再判死、结婚、生育。"""

    def __init__(self, population_cap=None, child_generation_from=None):
        cap = POPULATION_CAP if population_cap is None else population_cap
        self.population_cap = None if cap is None or cap <= 0 else int(cap)

        mode = CHILD_GENERATION_FROM if child_generation_from is None else child_generation_from
        if mode not in ('father', 'mother', 'max'):
            raise ValueError("child_generation_from 须为 'father'、'mother' 或 'max'")
        self.child_generation_from = mode

        self.people: dict[int, Person] = {}
        self.dead_archive: dict[int, Person] = {}
        self.next_id = 0
        self.current_month = 0
        self.alive_count = 0

        self.deaths_this_month = 0
        self.deaths_this_year = 0
        self.deaths_total = 0
        self.births_this_month = 0
        self.births_this_year = 0

        self._alive_cache: list[Person] | None = None

        self._sum_death_age_months = 0
        self._sum_first_marriage_age_months = 0
        self._first_marriage_count = 0
        self._prev_month_total = 0
        self._last_growth_rate = 0.0

        self._init_population()

    def _init_population(self):
        max_id = -1
        for person_id, gender, age_years, spouse_id, children, father_id, mother_id in INIT_POPULATION:
            person = Person(person_id, gender, age_years, spouse_id, children, father_id, mother_id)
            # 初始已婚人口缺少真实首婚年龄时，退化为“模拟开始时年龄”作为统计基线。
            if spouse_id != -1:
                person.first_marriage_month = 0
                self._sum_first_marriage_age_months += person.age_months
                self._first_marriage_count += 1
            self.people[person_id] = person
            if person_id > max_id:
                max_id = person_id
        self.next_id = max_id + 1
        self.alive_count = len(self.people)
        self._prev_month_total = self.alive_count
        self._invalidate_alive_cache()

    def _alloc_id(self):
        pid = self.next_id
        self.next_id += 1
        return pid

    def _invalidate_alive_cache(self):
        self._alive_cache = None

    def _alive_people(self) -> list[Person]:
        if self._alive_cache is None:
            self._alive_cache = [person for person in self.people.values() if person.alive]
        return self._alive_cache

    def _lookup_person(self, person_id: int) -> Person | None:
        """在存活字典和死亡归档中查找人员（用于亲属关系追溯）。"""
        person = self.people.get(person_id)
        if person is not None:
            return person
        return self.dead_archive.get(person_id)

    def _get_ancestors(self, p: Person, depth: int) -> set[int]:
        """收集 p 向上 depth 代的所有祖先 ID（不含 p 自身）。"""
        ancestors: set[int] = set()
        frontier = [p.id]
        for _ in range(depth):
            next_frontier = []
            for person_id in frontier:
                person = self._lookup_person(person_id)
                if person is None:
                    continue
                for parent_id in (person.father_id, person.mother_id):
                    if parent_id != -1 and parent_id not in ancestors:
                        ancestors.add(parent_id)
                        next_frontier.append(parent_id)
            frontier = next_frontier
            if not frontier:
                break
        return ancestors

    def _record_first_marriage(self, person: Person):
        if person.first_marriage_month == -1:
            person.first_marriage_month = self.current_month
            self._sum_first_marriage_age_months += person.age_months
            self._first_marriage_count += 1

    def _build_ancestor_cache(self, people: list[Person]) -> dict[int, set[int]]:
        return {
            person.id: self._get_ancestors(person, KIN_CHECK_DEPTH)
            for person in people
        }

    def _is_kin_with_cache(
        self,
        first_person: Person,
        second_person: Person,
        ancestor_cache: dict[int, set[int]],
    ) -> bool:
        first_ancestors = ancestor_cache[first_person.id]
        second_ancestors = ancestor_cache[second_person.id]
        if second_person.id in first_ancestors or first_person.id in second_ancestors:
            return True
        if first_ancestors & second_ancestors:
            return True
        return False

    # ------ 每月步骤 ------

    def step(self):
        self.current_month += 1
        self.deaths_this_month = 0
        self.births_this_month = 0

        if self.current_month % 12 == 1:
            self.deaths_this_year = 0
            self.births_this_year = 0

        self._step_aging()
        self._step_death()
        self._step_marriage()
        self._step_pregnancy()
        self._step_archive()
        if self._prev_month_total > 0:
            self._last_growth_rate = (
                (self.alive_count - self._prev_month_total) / self._prev_month_total * 100.0
            )
        else:
            self._last_growth_rate = 0.0
        self._prev_month_total = self.alive_count

    def _step_aging(self):
        for person in self._alive_people():
            person.age_months += 1

    def _step_death(self):
        """三层死亡模型：
        1) 达到 DOMINO_MAX_HUMAN_AGE 必死
        2) 超过 DOMINO_MAX_BEAR_AGE 按年龄递增概率（二次曲线）死亡
        3) 0~1 岁婴幼儿死亡概率
        4) 所有年龄段意外死亡
        """
        dead_person_ids = []

        for person in self._alive_people():
            if person.age_months >= MAX_HUMAN_AGE_MONTHS:
                dead_person_ids.append(person.id)
                continue

            if person.age_months >= MAX_BEAR_AGE_MONTHS:
                age_range_months = MAX_HUMAN_AGE_MONTHS - MAX_BEAR_AGE_MONTHS
                age_ratio = (person.age_months - MAX_BEAR_AGE_MONTHS) / age_range_months
                death_prob_year = (
                    BASE_DEATH_PROB_PER_YEAR
                    + (1.0 - BASE_DEATH_PROB_PER_YEAR) * (age_ratio ** 2)
                )
                if random.random() < _yearly_to_monthly_prob(death_prob_year):
                    dead_person_ids.append(person.id)
                    continue

            if person.age_months <= 12 and random.random() < MONTHLY_INFANT_DEATH_PROBABILITY:
                dead_person_ids.append(person.id)
                continue

            if random.random() < MONTHLY_ACCIDENT_DEATH_PROBABILITY:
                dead_person_ids.append(person.id)

        for person_id in dead_person_ids:
            person = self.people[person_id]
            person.alive = False
            self.alive_count -= 1
            self.deaths_this_month += 1
            self.deaths_this_year += 1
            self.deaths_total += 1
            self._sum_death_age_months += person.age_months

            if person.spouse_id != -1:
                spouse = self._lookup_person(person.spouse_id)
                if spouse and spouse.alive:
                    spouse.spouse_id = -1
                    spouse.widowed = True
                person.spouse_id = -1

        if dead_person_ids:
            self._invalidate_alive_cache()

    def _step_marriage(self):
        """适龄单身男女随机配对；男女婚龄上限可不同；年龄相近优先匹配。"""
        alive_people = self._alive_people()
        unmarried_males = [
            person for person in alive_people
            if person.gender == MALE and person.spouse_id == -1
            and MIN_BEAR_AGE_MONTHS <= person.age_months <= MALE_MARRIAGE_MAX_AGE_MONTHS
        ]
        unmarried_females = [
            person for person in alive_people
            if person.gender == FEMALE and person.spouse_id == -1
            and MIN_BEAR_AGE_MONTHS <= person.age_months <= FEMALE_MARRIAGE_MAX_AGE_MONTHS
        ]

        random.shuffle(unmarried_males)

        ancestor_cache = self._build_ancestor_cache(unmarried_males + unmarried_females)
        matched_female_ids: set[int] = set()
        has_new_marriage = False
        for male_person in unmarried_males:
            marriage_probability = (
                MONTHLY_REMARRIAGE_PROBABILITY
                if male_person.widowed
                else MONTHLY_MARRIAGE_PROBABILITY
            )
            if random.random() >= marriage_probability:
                continue

            candidate_females = [
                female_person for female_person in unmarried_females
                if female_person.id not in matched_female_ids
                and not self._is_kin_with_cache(male_person, female_person, ancestor_cache)
            ]
            if not candidate_females:
                continue

            age_difference_weights = [
                1.0 / (1.0 + abs(male_person.age_months - female_person.age_months) / 12.0)
                for female_person in candidate_females
            ]
            chosen_female = random.choices(
                candidate_females,
                weights=age_difference_weights,
                k=1,
            )[0]

            male_person.spouse_id = chosen_female.id
            chosen_female.spouse_id = male_person.id
            self._record_first_marriage(male_person)
            self._record_first_marriage(chosen_female)
            male_person.widowed = False
            chosen_female.widowed = False
            matched_female_ids.add(chosen_female.id)
            has_new_marriage = True

        if has_new_marriage:
            self._invalidate_alive_cache()

    def _step_pregnancy(self):
        """育龄夫妻按年龄抛物线衰减概率怀孕。"""
        peak_fertility_age = (DOMINO_MIN_BEAR_AGE + DOMINO_MAX_BEAR_AGE) / 2.0
        fertility_half_span = (DOMINO_MAX_BEAR_AGE - DOMINO_MIN_BEAR_AGE) / 2.0

        processed_spouse_ids: set[int] = set()
        has_new_birth = False
        for person in self._alive_people():
            if person.spouse_id == -1 or person.id in processed_spouse_ids:
                continue

            spouse = self._lookup_person(person.spouse_id)
            if not spouse or not spouse.alive:
                continue

            processed_spouse_ids.add(person.id)
            processed_spouse_ids.add(spouse.id)

            mother = person if person.gender == FEMALE else spouse
            father = spouse if mother is person else person

            if not (MIN_BEAR_AGE_MONTHS <= mother.age_months <= MAX_BEAR_AGE_MONTHS):
                continue
            if not (MIN_BEAR_AGE_MONTHS <= father.age_months <= MAX_BEAR_AGE_MONTHS):
                continue

            if (len(mother.children) >= MAX_CHILDREN_PER_COUPLE
                    or len(father.children) >= MAX_CHILDREN_PER_COUPLE):
                continue

            if (self.current_month - mother.last_birth_sim_month) < MIN_BIRTH_INTERVAL:
                continue

            mother_age_years = mother.age_months / 12.0
            fertility_age_factor = max(
                0.0,
                1.0 - ((mother_age_years - peak_fertility_age) / fertility_half_span) ** 2,
            )
            yearly_pregnancy_probability = PREGNANCY_PROB_PER_YEAR * fertility_age_factor
            if random.random() >= _yearly_to_monthly_prob(yearly_pregnancy_probability):
                continue

            remaining_mother_children_quota = MAX_CHILDREN_PER_COUPLE - len(mother.children)
            remaining_father_children_quota = MAX_CHILDREN_PER_COUPLE - len(father.children)
            max_birth_count_this_pregnancy = min(
                MAX_CHILDREN_PER_BIRTH,
                remaining_mother_children_quota,
                remaining_father_children_quota,
            )
            if max_birth_count_this_pregnancy <= 0:
                continue

            if self.population_cap is not None:
                if self.alive_count >= self.population_cap:
                    continue
                remaining_population_room = self.population_cap - self.alive_count
                max_birth_count_this_pregnancy = min(
                    max_birth_count_this_pregnancy,
                    remaining_population_room,
                )
                if max_birth_count_this_pregnancy <= 0:
                    continue

            newborn_count = random.randint(1, max_birth_count_this_pregnancy)
            for _ in range(newborn_count):
                baby_id = self._alloc_id()
                baby_gender = MALE if random.random() < MALE_BIRTH_RANDOM_THRESHOLD else FEMALE
                if self.child_generation_from == 'father':
                    base_generation = father.generation
                elif self.child_generation_from == 'mother':
                    base_generation = mother.generation
                else:
                    base_generation = max(father.generation, mother.generation)
                baby_generation = min(base_generation + 1, MAX_GENERATION)
                baby = Person(
                    baby_id, baby_gender, 0,
                    father_id=father.id, mother_id=mother.id,
                    generation=baby_generation,
                )
                self.people[baby_id] = baby
                self.alive_count += 1
                mother.children.append(baby_id)
                father.children.append(baby_id)
                self.births_this_month += 1
                self.births_this_year += 1
                has_new_birth = True

            mother.last_birth_sim_month = self.current_month

        if has_new_birth:
            self._invalidate_alive_cache()

    def _step_archive(self):
        """每满一年将死者从活跃字典迁移到归档字典，减少日常遍历量。"""
        if self.current_month % 12 != 0:
            return
        archived_person_ids = [person_id for person_id, person in self.people.items() if not person.alive]
        for person_id in archived_person_ids:
            self.dead_archive[person_id] = self.people.pop(person_id)
        if archived_person_ids:
            self._invalidate_alive_cache()

    # ------ 统计 ------

    def get_stats(self):
        alive_people = self._alive_people()
        total = self.alive_count
        male_count = sum(1 for person in alive_people if person.gender == MALE)
        female_count = total - male_count
        child_count = 0
        fertile_count = 0
        elder_count = 0

        couples: set[tuple[int, int]] = set()
        for person in alive_people:
            if person.spouse_id != -1:
                key = (min(person.id, person.spouse_id), max(person.id, person.spouse_id))
                couples.add(key)
        couple_count = len(couples)

        generation_counts: dict[int, int] = {}
        age_bucket_male = [0] * (DOMINO_MAX_HUMAN_AGE + 1)
        age_bucket_female = [0] * (DOMINO_MAX_HUMAN_AGE + 1)
        for person in alive_people:
            generation = person.generation
            generation_counts[generation] = generation_counts.get(generation, 0) + 1
            if person.age_months < MIN_BEAR_AGE_MONTHS:
                child_count += 1
            elif person.age_months < MAX_BEAR_AGE_MONTHS:
                fertile_count += 1
            else:
                elder_count += 1

            age_year = min(person.age_years, DOMINO_MAX_HUMAN_AGE)
            if person.gender == MALE:
                age_bucket_male[age_year] += 1
            else:
                age_bucket_female[age_year] += 1

        average_death_age = (
            (self._sum_death_age_months / self.deaths_total / 12.0)
            if self.deaths_total > 0 else 0.0
        )
        average_first_marriage_age = (
            (self._sum_first_marriage_age_months / self._first_marriage_count / 12.0)
            if self._first_marriage_count > 0 else 0.0
        )
        dependency_ratio = (
            ((child_count + elder_count) / fertile_count)
            if fertile_count > 0 else 0.0
        )
        return {
            'total': total,
            'males': male_count,
            'females': female_count,
            'couple_count': couple_count,
            'below_bear_age': child_count,
            'fertile_age': fertile_count,
            'over_max_bear_age': elder_count,
            'age_male': age_bucket_male,
            'age_female': age_bucket_female,
            'generation_counts': generation_counts,
            'avg_death_age': average_death_age,
            'avg_first_marriage_age': average_first_marriage_age,
            'dependency_ratio': dependency_ratio,
            'growth_rate': self._last_growth_rate,
        }


# ============================================================
#  输出（终端列宽：CJK 等宽字符按 East Asian Width 计为 2）
# ============================================================

def _disp_w(ch: str) -> int:
    east_asian_width = unicodedata.east_asian_width(ch)
    return 2 if east_asian_width in ('F', 'W') else 1


def _term_width(s: str) -> int:
    return sum(_disp_w(c) for c in s)


def _pad_left_disp(s: str, width: int) -> str:
    return ' ' * max(0, width - _term_width(s)) + s


def _pad_right_disp(s: str, width: int) -> str:
    return s + ' ' * max(0, width - _term_width(s))


def print_stats(sim: Simulation, stats: dict):
    simulation_year = (sim.current_month - 1) // 12
    month_in_year = (sim.current_month - 1) % 12 + 1
    total_population = stats['total']
    male_population = stats['males']
    female_population = stats['females']
    male_ratio = male_population / total_population * 100 if total_population else 0
    female_ratio = female_population / total_population * 100 if total_population else 0
    child_population = stats['below_bear_age']
    fertile_population = stats['fertile_age']
    elder_population = stats['over_max_bear_age']
    child_ratio = child_population / total_population * 100 if total_population else 0
    fertile_ratio = fertile_population / total_population * 100 if total_population else 0
    elder_ratio = elder_population / total_population * 100 if total_population else 0

    N = 8
    COL_SEP = '  |  '
    AGE_COL = 6

    sys.stdout.write('\033[2J\033[H')

    print(f'===== 人口演变模拟 | 第 {sim.current_month} 月 '
          f'(第 {simulation_year} 年 {month_in_year} 月) =====')
    print()
    _stat_labels = (
        '总人口', '男性', '女性',
        '现有夫妻', '本月增长率', '本年出生', '本年死亡', '累计死亡',
        '平均寿命', '平均初婚',
        '儿童', '成年', '老人', '抚养比',
    )
    label_width = max(_term_width(label) for label in _stat_labels)
    print(f'{_pad_right_disp("总人口", label_width)}  {total_population}')
    print(f'{_pad_right_disp("男性", label_width)}  {male_population} ({male_ratio:.1f}%)')
    print(f'{_pad_right_disp("女性", label_width)}  {female_population} ({female_ratio:.1f}%)')
    print()
    print(f'{_pad_right_disp("现有夫妻", label_width)}  {stats["couple_count"]}')
    print(f'{_pad_right_disp("本月增长率", label_width)}  {stats["growth_rate"]:+.2f}%')
    print(f'{_pad_right_disp("本年出生", label_width)}  {sim.births_this_year}')
    print(f'{_pad_right_disp("本年死亡", label_width)}  {sim.deaths_this_year}')
    print(f'{_pad_right_disp("累计死亡", label_width)}  {sim.deaths_total}')
    print()
    print(f'{_pad_right_disp("平均寿命", label_width)}  {stats["avg_death_age"]:.1f} 岁')
    print(f'{_pad_right_disp("平均初婚", label_width)}  {stats["avg_first_marriage_age"]:.1f} 岁')
    print()
    print(f'{_pad_right_disp("儿童", label_width)}  {child_population} ({child_ratio:.1f}%)')
    print(f'{_pad_right_disp("成年", label_width)}  {fertile_population} ({fertile_ratio:.1f}%)')
    print(f'{_pad_right_disp("老人", label_width)}  {elder_population} ({elder_ratio:.1f}%)')
    print(f'{_pad_right_disp("抚养比", label_width)}  {stats["dependency_ratio"]:.2f}')
    print()
    print(
        f'{_pad_left_disp("年龄", AGE_COL)}{COL_SEP}'
        f'{_pad_left_disp("男性", N)}{COL_SEP}'
        f'{_pad_left_disp("女性", N)}{COL_SEP}'
        f'{_pad_left_disp("合计", N)}'
    )
    print('-' * (AGE_COL + len(COL_SEP) + N + len(COL_SEP) + N + len(COL_SEP) + N))
    for age in range(DOMINO_MAX_HUMAN_AGE + 1):
        male_count = stats['age_male'][age]
        female_count = stats['age_female'][age]
        total_count = male_count + female_count
        if total_count > 0:
            print(
                f'{_pad_left_disp(str(age), AGE_COL)}{COL_SEP}'
                f'{_pad_left_disp(str(male_count), N)}{COL_SEP}'
                f'{_pad_left_disp(str(female_count), N)}{COL_SEP}'
                f'{_pad_left_disp(str(total_count), N)}'
            )

    sys.stdout.flush()


def build_csv_header():
    header = [
        '月份', '年', '月', '总人口',
        '男性人口', '女性人口', '男性比例', '女性比例',
        '夫妻数', '本月增长率', '本年出生', '本年死亡', '累计死亡',
        '平均寿命', '平均初婚年龄', '抚养比',
        '儿童人口', '成年人口', '老人人口',
    ]
    for age in range(DOMINO_MAX_HUMAN_AGE + 1):
        header.append(f'{age}岁男')
        header.append(f'{age}岁女')
    for g in range(1, MAX_GENERATION + 1):
        header.append(f'第{g}代')
    return header


def build_csv_row(sim: Simulation, stats: dict):
    total_population = stats['total']
    male_population = stats['males']
    female_population = stats['females']
    simulation_year = (sim.current_month - 1) // 12
    month_in_year = (sim.current_month - 1) % 12 + 1

    row = [
        sim.current_month, simulation_year, month_in_year, total_population,
        male_population, female_population,
        round(male_population / total_population * 100, 1) if total_population else 0,
        round(female_population / total_population * 100, 1) if total_population else 0,
        stats['couple_count'],
        round(stats['growth_rate'], 2),
        sim.births_this_year,
        sim.deaths_this_year, sim.deaths_total,
        round(stats['avg_death_age'], 1),
        round(stats['avg_first_marriage_age'], 1),
        round(stats['dependency_ratio'], 2),
        stats['below_bear_age'],
        stats['fertile_age'],
        stats['over_max_bear_age'],
    ]
    for age in range(DOMINO_MAX_HUMAN_AGE + 1):
        row.append(stats['age_male'][age])
        row.append(stats['age_female'][age])
    gen_counts = stats['generation_counts']
    for g in range(1, MAX_GENERATION + 1):
        row.append(gen_counts.get(g, 0))
    return row


# ============================================================
#  交互：空格暂停 / 再按空格继续（需交互式终端，Unix）
# ============================================================


def _stdin_enable_cbreak():
    global _stdin_termios_backup, _stdin_cbreak_enabled
    if not sys.stdin.isatty():
        return False
    try:
        fd = sys.stdin.fileno()
        _stdin_termios_backup = termios.tcgetattr(fd)
        tty.setcbreak(fd)
        _stdin_cbreak_enabled = True
        return True
    except (OSError, AttributeError, termios.error):
        return False


def _stdin_restore():
    global _stdin_termios_backup, _stdin_cbreak_enabled
    if not _stdin_cbreak_enabled or _stdin_termios_backup is None:
        return
    try:
        fd = sys.stdin.fileno()
        termios.tcsetattr(fd, termios.TCSADRAIN, _stdin_termios_backup)
    except OSError:
        pass
    _stdin_termios_backup = None
    _stdin_cbreak_enabled = False


def wait_inter_step_or_space(seconds: float) -> bool:
    if not _stdin_cbreak_enabled:
        time.sleep(seconds)
        return False
    end_time = time.time() + seconds
    while True:
        remaining_seconds = end_time - time.time()
        if remaining_seconds <= 0:
            return False
        readable_streams, _, _ = select.select([sys.stdin], [], [], min(0.05, remaining_seconds))
        if readable_streams:
            pressed_key = sys.stdin.read(1)
            if pressed_key == ' ':
                return True


def wait_space_to_resume():
    while True:
        pressed_key = sys.stdin.read(1)
        if pressed_key == ' ':
            return


# ============================================================
#  主循环
# ============================================================

def main():
    argument_parser = argparse.ArgumentParser(description='人口演变模拟')
    argument_parser.add_argument(
        '--cap', type=int, default=None, metavar='N',
        help='存活人口上限；0 为不限制；默认使用脚本内 POPULATION_CAP',
    )
    argument_parser.add_argument(
        '--child-gen-from', choices=('father', 'mother', 'max'), default=None,
        metavar='MODE',
        help='子女世代基准：父系 father / 母系 mother / 父母较大 max',
    )
    argument_parser.add_argument(
        '--seed', type=int, default=None, metavar='S',
        help='随机种子，设置后模拟结果可复现',
    )
    parsed_args = argument_parser.parse_args()

    if parsed_args.seed is not None:
        random.seed(parsed_args.seed)

    population_cap = POPULATION_CAP if parsed_args.cap is None else parsed_args.cap
    simulation = Simulation(
        population_cap=population_cap,
        child_generation_from=parsed_args.child_gen_from,
    )

    csv_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), '人口演变.csv')
    with open(csv_path, 'w', newline='', encoding='utf-8-sig') as csv_file:
        csv_writer = csv.writer(csv_file)
        csv_writer.writerow(build_csv_header())
        csv_file.flush()

        print('人口演变模拟启动，按 Ctrl+C 停止；交互终端下按空格暂停，再按空格继续…')
        child_generation_label = {
            'father': '父系',
            'mother': '母系',
            'max': '父母较大世代',
        }[simulation.child_generation_from]
        print(f'子女世代基准: {child_generation_label} ({simulation.child_generation_from})')
        if simulation.population_cap is not None:
            print(f'人口上限: {simulation.population_cap}（达到后仅能通过死亡下降后再生育）')
        else:
            print('人口上限: 不限制')
        if parsed_args.seed is not None:
            print(f'随机种子: {parsed_args.seed}')
        print(f'CSV 输出文件: {csv_path}')
        time.sleep(1)

        keyboard_pause_enabled = _stdin_enable_cbreak()
        if not keyboard_pause_enabled:
            print('（当前不是交互终端：无法使用空格暂停，仍按间隔自动迭代）')

        try:
            while True:
                simulation.step()
                simulation_stats = simulation.get_stats()

                print_stats(simulation, simulation_stats)
                for generation in sorted(simulation_stats['generation_counts'].keys()):
                    generation_population = simulation_stats['generation_counts'][generation]
                    if generation_population > 0:
                        print(f'第 {generation} 代 人口: {generation_population}')
                csv_writer.writerow(build_csv_row(simulation, simulation_stats))
                csv_file.flush()

                if simulation_stats['total'] == 0:
                    print('\n人口灭绝，模拟结束。')
                    break

                if wait_inter_step_or_space(INTER_STEP_SLEEP_SEC):
                    print('\n已暂停，按空格继续…', end='', flush=True)
                    wait_space_to_resume()
                    print()
        except KeyboardInterrupt:
            print('\n\n模拟已停止。')
        finally:
            _stdin_restore()
            print(f'数据已保存到: {csv_path}')


if __name__ == '__main__':
    main()
